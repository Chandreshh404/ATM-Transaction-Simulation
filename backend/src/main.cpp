
#include <iostream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "database/Database.h"
#include "services/AuthService.h"
#include "security/SessionManager.h"

using json = nlohmann::json;

int main() {
    Database database;

    if (!database.connect("database/atm.db")) {
        std::cerr << "Could not connect to ATM database." << std::endl;
        return 1;
    }

    if (!database.initializeSchema("database/schema.sql")) {
        std::cerr << "Database initialization failed." << std::endl;
        return 1;
    }

    AuthService authService(database);
    SessionManager sessionManager;

    httplib::Server server;

    // Health check
    server.Get("/api/health",
        [](const httplib::Request& req, httplib::Response& res) {

        json response = {
            {"status", "success"},
            {"message", "ATM Backend is running"},
            {"language", "C++17"}
        };

        res.set_content(response.dump(4), "application/json");
    });

    // Database health check
    server.Get("/api/health/db",
        [](const httplib::Request& req, httplib::Response& res) {

        json response = {
            {"status", "success"},
            {"message", "SQLite database is connected and initialized"}
        };

        res.set_content(response.dump(4), "application/json");
    });


// User registration
server.Options("/api/auth/register",
    [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "600");
    });

server.Post("/api/auth/register",
    [&authService](const httplib::Request& req, httplib::Response& res) {

        res.set_header("Access-Control-Allow-Origin", "*");

        try {
            json request = json::parse(req.body);

            if (!request.contains("fullName") ||
                !request.contains("email") ||
                !request.contains("pin") ||
                !request["fullName"].is_string() ||
                !request["email"].is_string() ||
                !request["pin"].is_string()) {

                res.status = 400;
                res.set_content(
                    json({
                        {"status", "error"},
                        {"message", "Full name, email and PIN are required."}
                    }).dump(4),
                    "application/json"
                );
                return;
            }

            AuthResult result = authService.registerUser(
                request["fullName"].get<std::string>(),
                request["email"].get<std::string>(),
                request["pin"].get<std::string>()
            );

            json response = {
                {"status", result.success ? "success" : "error"},
                {"message", result.message}
            };

            if (result.success) {
                res.status = 201;
                response["userId"] = result.userId;
                response["accountNumber"] = result.accountNumber;
            } else {
                res.status = (
                    result.message == "This email is already registered."
                    ? 409 : 400
                );
            }

            res.set_content(response.dump(4), "application/json");
        }
        catch (const json::exception&) {
            res.status = 400;
            res.set_content(
                json({
                    {"status", "error"},
                    {"message", "Invalid JSON request."}
                }).dump(4),
                "application/json"
            );
        }
    });



// User login with session creation
server.Options("/api/auth/login",
    [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "600");
    });

server.Post("/api/auth/login",
    [&authService, &sessionManager](
        const httplib::Request& req,
        httplib::Response& res
    ) {

    res.set_header("Access-Control-Allow-Origin", "*");

    try {
        json request = json::parse(req.body);

        if (!request.contains("email") ||
            !request.contains("pin") ||
            !request["email"].is_string() ||
            !request["pin"].is_string()) {

            res.status = 400;
            res.set_content(
                json({
                    {"status", "error"},
                    {"message", "Email and PIN are required."}
                }).dump(4),
                "application/json"
            );
            return;
        }

        AuthResult result = authService.loginUser(
            request["email"].get<std::string>(),
            request["pin"].get<std::string>()
        );

        if (!result.success) {
            res.status = 401;
            res.set_content(
                json({
                    {"status", "error"},
                    {"message", result.message}
                }).dump(4),
                "application/json"
            );
            return;
        }

        // Generate a session token after successful login.
        std::string token =
            sessionManager.createSession(result.userId);

        if (token.empty()) {
            res.status = 500;
            res.set_content(
                json({
                    {"status", "error"},
                    {"message", "Could not create login session."}
                }).dump(4),
                "application/json"
            );
            return;
        }

        json response = {
            {"status", "success"},
            {"message", "Login successful."},
            {"userId", result.userId},
            {"accountNumber", result.accountNumber},
            {"sessionToken", token},
            {"expiresInSeconds", 900}
        };

        res.status = 200;
        res.set_content(response.dump(4), "application/json");
    }
    catch (const json::exception&) {
        res.status = 400;
        res.set_content(
            json({
                {"status", "error"},
                {"message", "Invalid JSON request."}
            }).dump(4),
            "application/json"
        );
    }
});


    // CORS preflight for a separately hosted development frontend (e.g. VS Code Live Server).
    server.Options("/api/transfer",
        [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.set_header("Access-Control-Max-Age", "600");
        });

    // Transfer funds. The authenticated session determines the sender account;
    // the browser is never allowed to choose another user's sender account.
    server.Post("/api/transfer",
        [&database, &sessionManager](const httplib::Request& req,
                                     httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Cache-Control", "no-store");

            auto sendJson = [&res](int status, const json& body) {
                res.status = status;
                res.set_content(body.dump(4), "application/json");
            };

            std::string token = req.get_header_value("Authorization");
            const std::string prefix = "Bearer ";
            if (token.rfind(prefix, 0) == 0) token = token.substr(prefix.size());
            else token.clear();

            int userId = -1;
            if (token.empty() || !sessionManager.validateSession(token, userId)) {
                sendJson(401, {{"status", "error"},
                               {"message", "Invalid or expired session."}});
                return;
            }

            try {
                const json request = json::parse(req.body);
                if (!request.is_object() ||
                    !request.contains("targetAccount") ||
                    !request["targetAccount"].is_string() ||
                    !request.contains("amount") ||
                    !(request["amount"].is_number())) {
                    sendJson(400, {{"status", "error"},
                                   {"message", "Target account and numeric amount are required."}});
                    return;
                }

                const std::string targetAccount =
                    request["targetAccount"].get<std::string>();
                const long double amountRupees =
                    request["amount"].get<long double>();

                if (targetAccount.empty() || targetAccount.size() > 32 ||
                    !std::all_of(targetAccount.begin(), targetAccount.end(),
                        [](unsigned char c) { return std::isdigit(c); })) {
                    sendJson(400, {{"status", "error"},
                                   {"message", "Enter a valid target account number."}});
                    return;
                }
                if (!std::isfinite(amountRupees) || amountRupees <= 0.0L ||
                    amountRupees > static_cast<long double>(
                        std::numeric_limits<std::int64_t>::max()) / 100.0L) {
                    sendJson(400, {{"status", "error"},
                                   {"message", "Amount must be a positive valid number."}});
                    return;
                }

                const long double paiseValue = std::round(amountRupees * 100.0L);
                if (std::fabs(paiseValue - amountRupees * 100.0L) > 0.000001L) {
                    sendJson(400, {{"status", "error"},
                                   {"message", "Amount cannot have more than two decimal places."}});
                    return;
                }
                const auto amountPaise = static_cast<std::int64_t>(paiseValue);

                const std::string senderAccount = database.getAccountNumberForUser(userId);
                if (senderAccount.empty()) {
                    sendJson(400, {{"status", "error"},
                                   {"message", "No active sender account is available."}});
                    return;
                }
                if (senderAccount == targetAccount) {
                    sendJson(400, {{"status", "error"},
                                   {"message", "You cannot transfer money to your own account."}});
                    return;
                }

                const TransferResult result = database.transferFunds(
                    senderAccount, targetAccount, amountPaise);
                if (!result.success) {
                    sendJson(result.clientError ? 400 : 500,
                        {{"status", "error"}, {"message", result.message}});
                    return;
                }

                sendJson(200, {
                    {"status", "success"},
                    {"message", result.message},
                    {"senderAccount", senderAccount},
                    {"targetAccount", targetAccount},
                    {"transferredAmount", static_cast<double>(amountPaise) / 100.0},
                    {"balancePaise", result.senderBalancePaise},
                    {"balance", static_cast<double>(result.senderBalancePaise) / 100.0}
                });
            }
            catch (const json::exception&) {
                sendJson(400, {{"status", "error"},
                               {"message", "Invalid JSON request."}});
            }
        });


    // Shared CORS configuration for account endpoints.
    auto setCors = [](httplib::Response& res, const std::string& methods) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", methods);
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "600");
        res.set_header("Cache-Control", "no-store");
    };

    // Validate the session and retrieve the authenticated user ID.
    auto getUserId = [&sessionManager](const httplib::Request& req) {
        std::string token = req.get_header_value("Authorization");
        const std::string prefix = "Bearer ";

        if (token.rfind(prefix, 0) == 0) {
            token = token.substr(prefix.size());
        } else {
            token.clear();
        }

        int userId = -1;

        if (token.empty() ||
            !sessionManager.validateSession(token, userId)) {
            return -1;
        }

        return userId;
    };

    // Convert a valid rupee amount into integer paise.
    auto parseAmount = [](const json& value,
                          std::int64_t& amountPaise,
                          std::string& error) {
        if (!value.is_number()) {
            error = "A numeric amount is required.";
            return false;
        }

        const long double amount = value.get<long double>();

        if (!std::isfinite(amount) || amount <= 0.0L ||
            amount > static_cast<long double>(
                std::numeric_limits<std::int64_t>::max()) / 100.0L) {
            error = "Amount must be a positive valid number.";
            return false;
        }

        const long double paise = std::round(amount * 100.0L);

        if (std::fabs(paise - amount * 100.0L) > 0.000001L) {
            error = "Amount cannot have more than two decimal places.";
            return false;
        }

        amountPaise = static_cast<std::int64_t>(paise);

        if (amountPaise <= 0) {
            error = "Amount is too small.";
            return false;
        }

        return true;
    };

    // CORS preflight for account balance.
    server.Options("/api/account/balance",
        [&setCors](const httplib::Request&, httplib::Response& res) {
            setCors(res, "GET, OPTIONS");
            res.status = 204;
        });

    // Get authenticated user's account balance.
    server.Get("/api/account/balance",
        [&database, &getUserId, &setCors](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            setCors(res, "GET, OPTIONS");

            auto sendJson = [&res](int status, const json& body) {
                res.status = status;
                res.set_content(body.dump(4), "application/json");
            };

            const int userId = getUserId(req);

            if (userId == -1) {
                sendJson(401, {
                    {"status", "error"},
                    {"message", "Invalid or expired session."}
                });
                return;
            }

            const AccountBalance result = database.getAccountBalance(userId);

            if (!result.success) {
                sendJson(500, {
                    {"status", "error"},
                    {"message", result.message}
                });
                return;
            }

            sendJson(200, {
                {"status", "success"},
                {"message", "Account balance retrieved successfully."},
                {"accountNumber", result.accountNumber},
                {"balancePaise", result.balancePaise},
                {"balance", static_cast<double>(result.balancePaise) / 100.0}
            });
        });

    // CORS preflight for deposit.
    server.Options("/api/deposit",
        [&setCors](const httplib::Request&, httplib::Response& res) {
            setCors(res, "POST, OPTIONS");
            res.status = 204;
        });

    // Deposit money into authenticated user's account.
    server.Post("/api/deposit",
        [&database, &getUserId, &setCors, &parseAmount](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            setCors(res, "POST, OPTIONS");

            auto sendJson = [&res](int status, const json& body) {
                res.status = status;
                res.set_content(body.dump(4), "application/json");
            };

            const int userId = getUserId(req);

            if (userId == -1) {
                sendJson(401, {
                    {"status", "error"},
                    {"message", "Invalid or expired session."}
                });
                return;
            }

            try {
                const json request = json::parse(req.body);

                if (!request.is_object() || !request.contains("amount")) {
                    sendJson(400, {
                        {"status", "error"},
                        {"message", "Amount is required."}
                    });
                    return;
                }

                std::int64_t amountPaise = 0;
                std::string error;

                if (!parseAmount(request["amount"], amountPaise, error)) {
                    sendJson(400, {
                        {"status", "error"},
                        {"message", error}
                    });
                    return;
                }

                const MoneyOperationResult result =
                    database.depositFunds(userId, amountPaise);

                if (!result.success) {
                    sendJson(result.clientError ? 400 : 500, {
                        {"status", "error"},
                        {"message", result.message}
                    });
                    return;
                }

                sendJson(200, {
                    {"status", "success"},
                    {"message", result.message},
                    {"balancePaise", result.balancePaise},
                    {"balance", static_cast<double>(result.balancePaise) / 100.0}
                });
            }
            catch (const json::exception&) {
                sendJson(400, {
                    {"status", "error"},
                    {"message", "Invalid JSON request."}
                });
            }
        });

    // CORS preflight for withdrawal.
    server.Options("/api/withdraw",
        [&setCors](const httplib::Request&, httplib::Response& res) {
            setCors(res, "POST, OPTIONS");
            res.status = 204;
        });

    // Withdraw money from authenticated user's account.
    server.Post("/api/withdraw",
        [&database, &getUserId, &setCors, &parseAmount](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            setCors(res, "POST, OPTIONS");

            auto sendJson = [&res](int status, const json& body) {
                res.status = status;
                res.set_content(body.dump(4), "application/json");
            };

            const int userId = getUserId(req);

            if (userId == -1) {
                sendJson(401, {
                    {"status", "error"},
                    {"message", "Invalid or expired session."}
                });
                return;
            }

            try {
                const json request = json::parse(req.body);

                if (!request.is_object() || !request.contains("amount")) {
                    sendJson(400, {
                        {"status", "error"},
                        {"message", "Amount is required."}
                    });
                    return;
                }

                std::int64_t amountPaise = 0;
                std::string error;

                if (!parseAmount(request["amount"], amountPaise, error)) {
                    sendJson(400, {
                        {"status", "error"},
                        {"message", error}
                    });
                    return;
                }

                const MoneyOperationResult result =
                    database.withdrawFunds(userId, amountPaise);

                if (!result.success) {
                    sendJson(result.clientError ? 400 : 500, {
                        {"status", "error"},
                        {"message", result.message}
                    });
                    return;
                }

                sendJson(200, {
                    {"status", "success"},
                    {"message", result.message},
                    {"balancePaise", result.balancePaise},
                    {"balance", static_cast<double>(result.balancePaise) / 100.0}
                });
            }
            catch (const json::exception&) {
                sendJson(400, {
                    {"status", "error"},
                    {"message", "Invalid JSON request."}
                });
            }
        });

    // CORS preflight for transaction history.
    server.Options("/api/transactions",
        [&setCors](const httplib::Request&, httplib::Response& res) {
            setCors(res, "GET, OPTIONS");
            res.status = 204;
        });

    // Get authenticated user's transaction history.
    server.Get("/api/transactions",
        [&database, &getUserId, &setCors](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            setCors(res, "GET, OPTIONS");

            auto sendJson = [&res](int status, const json& body) {
                res.status = status;
                res.set_content(body.dump(4), "application/json");
            };

            const int userId = getUserId(req);

            if (userId == -1) {
                sendJson(401, {
                    {"status", "error"},
                    {"message", "Invalid or expired session."}
                });
                return;
            }

            int limit = 20;
            int offset = 0;

            try {
                if (req.has_param("limit")) {
                    const std::string value = req.get_param_value("limit");
                    std::size_t position = 0;
                    const long long parsed = std::stoll(value, &position);

                    if (position != value.size() || parsed < 1 || parsed > 100) {
                        throw std::invalid_argument("Invalid limit");
                    }

                    limit = static_cast<int>(parsed);
                }

                if (req.has_param("offset")) {
                    const std::string value = req.get_param_value("offset");
                    std::size_t position = 0;
                    const long long parsed = std::stoll(value, &position);

                    if (position != value.size() || parsed < 0 || parsed > 1000000) {
                        throw std::invalid_argument("Invalid offset");
                    }

                    offset = static_cast<int>(parsed);
                }
            }
            catch (const std::exception&) {
                sendJson(400, {
                    {"status", "error"},
                    {"message", "Invalid pagination parameters. Limit must be 1-100 and offset must be non-negative."}
                });
                return;
            }

            const TransactionHistory result =
                database.getTransactionHistory(userId, limit, offset);

            if (!result.success) {
                sendJson(500, {
                    {"status", "error"},
                    {"message", result.message}
                });
                return;
            }

            json transactions = json::array();

            for (const auto& transaction : result.transactions) {
                transactions.push_back({
                    {"id", transaction.id},
                    {"transactionType", transaction.transactionType},
                    {"amountPaise", transaction.amountPaise},
                    {"amount", static_cast<double>(transaction.amountPaise) / 100.0},
                    {"relatedAccountNumber", transaction.relatedAccountNumber},
                    {"description", transaction.description},
                    {"createdAt", transaction.createdAt}
                });
            }

            sendJson(200, {
                {"status", "success"},
                {"message", "Transaction history retrieved successfully."},
                {"limit", limit},
                {"offset", offset},
                {"transactions", transactions}
            });
        });



// User logout
server.Options("/api/auth/logout",
    [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "600");
    });

server.Post("/api/auth/logout",
    [&sessionManager](
        const httplib::Request& req,
        httplib::Response& res
    ) {

    res.set_header("Access-Control-Allow-Origin", "*");

    std::string token = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";

    if (token.rfind(prefix, 0) == 0) {
        token = token.substr(prefix.length());
    } else {
        token.clear();
    }

    if (token.empty() ||
        !sessionManager.invalidateSession(token)) {

        res.status = 401;
        res.set_content(
            json({
                {"status", "error"},
                {"message", "Invalid or expired session."}
            }).dump(4),
            "application/json"
        );
        return;
    }

    res.status = 200;
    res.set_content(
        json({
            {"status", "success"},
            {"message", "Logout successful."}
        }).dump(4),
        "application/json"
    );
});




// Validate an existing session
server.Options("/api/auth/validate",
    [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "600");
    });

server.Get("/api/auth/validate",
    [&sessionManager](
        const httplib::Request& req,
        httplib::Response& res
    ) {

    res.set_header("Access-Control-Allow-Origin", "*");

    std::string token = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";

    if (token.rfind(prefix, 0) == 0) {
        token = token.substr(prefix.length());
    } else {
        token.clear();
    }

    int userId = -1;

    if (token.empty() ||
        !sessionManager.validateSession(token, userId)) {

        res.status = 401;
        res.set_content(
            json({
                {"status", "error"},
                {"message", "Invalid or expired session."}
            }).dump(4),
            "application/json"
        );
        return;
    }

    res.status = 200;
    res.set_content(
        json({
            {"status", "success"},
            {"message", "Session is valid."},
            {"userId", userId}
        }).dump(4),
        "application/json"
    );
});



    std::cout << "\n====================================\n";
    std::cout << " ATM TRANSACTION SIMULATION SYSTEM\n";
    std::cout << "====================================\n";

    std::cout << "Database initialization complete.\n";
    std::cout << "Authentication service initialized.\n";
    std::cout << "Session manager initialized.\n";

    std::cout << "Server: http://localhost:8080\n";

    std::cout << "\nAvailable API endpoints:\n";
    std::cout << "GET  /api/health\n";
    std::cout << "GET  /api/health/db\n";
    std::cout << "POST /api/auth/register\n";
    std::cout << "POST /api/auth/login\n";
    std::cout << "POST /api/transfer\n";

        std::cout << "GET  /api/account/balance\n";
    std::cout << "POST /api/deposit\n";
    std::cout << "POST /api/withdraw\n";
    std::cout << "GET  /api/transactions\n";

    if (!server.listen("127.0.0.1", 8080)) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    return 0;
}
