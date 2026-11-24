#include "database/Database.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>

namespace {

bool executeSql(sqlite3* db, const char* sql) {
    char* errorMessage = nullptr;

    const int result = sqlite3_exec(
        db, sql, nullptr, nullptr, &errorMessage
    );

    if (result != SQLITE_OK) {
        std::cerr << "SQLite error: "
                  << (errorMessage ? errorMessage : sqlite3_errmsg(db))
                  << std::endl;

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

void rollback(sqlite3* db) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
}

} // namespace

Database::Database() : db(nullptr) {}

Database::~Database() {
    disconnect();
}

bool Database::connect(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(dbMutex);

    // Do not overwrite an existing SQLite connection.
    if (db != nullptr) {
        std::cerr << "Database is already connected." << std::endl;
        return false;
    }

    sqlite3* openedDb = nullptr;

    const int openResult = sqlite3_open_v2(
        dbPath.c_str(),
        &openedDb,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX,
        nullptr
    );

    if (openResult != SQLITE_OK) {
        std::cerr << "Database connection failed: "
                  << (openedDb
                      ? sqlite3_errmsg(openedDb)
                      : "Unable to allocate SQLite handle")
                  << std::endl;

        if (openedDb != nullptr) {
            sqlite3_close_v2(openedDb);
        }

        return false;
    }

    // Publish the connection only after SQLite has opened it successfully.
    db = openedDb;

    if (sqlite3_busy_timeout(db, 5000) != SQLITE_OK) {
        std::cerr << "Failed to configure SQLite busy timeout: "
                  << sqlite3_errmsg(db) << std::endl;

        sqlite3_close_v2(db);
        db = nullptr;
        return false;
    }

    // Foreign-key enforcement is connection-specific in SQLite.
    if (!executeSql(db, "PRAGMA foreign_keys = ON;")) {
        std::cerr << "Failed to enable SQLite foreign keys." << std::endl;

        sqlite3_close_v2(db);
        db = nullptr;
        return false;
    }

    // Verify that foreign-key enforcement is actually enabled.
    sqlite3_stmt* pragmaStatement = nullptr;
    const int prepareResult = sqlite3_prepare_v2(
        db,
        "PRAGMA foreign_keys;",
        -1,
        &pragmaStatement,
        nullptr
    );

    bool foreignKeysEnabled = false;

    if (prepareResult == SQLITE_OK &&
        sqlite3_step(pragmaStatement) == SQLITE_ROW) {
        foreignKeysEnabled =
            sqlite3_column_int(pragmaStatement, 0) == 1;
    }

    if (pragmaStatement != nullptr) {
        sqlite3_finalize(pragmaStatement);
    }

    if (!foreignKeysEnabled) {
        std::cerr << "SQLite foreign-key enforcement is not enabled."
                  << std::endl;

        sqlite3_close_v2(db);
        db = nullptr;
        return false;
    }

    std::cout << "SQLite database connected successfully!" << std::endl;
    return true;
}

void Database::disconnect() {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (db == nullptr) {
        return;
    }

    // close_v2 safely defers final closure if an outstanding statement
    // still exists. All statements in this class should nevertheless be
    // finalized before their operation returns.
    const int closeResult = sqlite3_close_v2(db);

    if (closeResult != SQLITE_OK) {
        std::cerr << "SQLite database close failed: "
                  << sqlite3_errstr(closeResult) << std::endl;
        return;
    }

    db = nullptr;
}

bool Database::execute(const std::string& sql) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (db == nullptr) {
        std::cerr << "Database is not connected." << std::endl;
        return false;
    }

    return executeSql(db, sql.c_str());
}

bool Database::initializeSchema(const std::string& schemaPath) {
    std::ifstream schemaFile(schemaPath);

    if (!schemaFile.is_open()) {
        std::cerr << "Could not open schema file: "
                  << schemaPath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << schemaFile.rdbuf();

    const std::string sql = buffer.str();

    if (sql.empty()) {
        std::cerr << "Schema file is empty." << std::endl;
        return false;
    }

    if (!execute(sql)) {
        std::cerr << "Failed to initialize database tables." << std::endl;
        return false;
    }

    std::cout << "Database tables initialized successfully!" << std::endl;

    return true;
}

// Callers must lock getMutex() before using the returned SQLite handle.
// The returned pointer is non-owning and must never be closed by the caller.
sqlite3* Database::getConnection() {
    return db;
}

std::mutex& Database::getMutex() {
    return dbMutex;
}

std::string Database::getAccountNumberForUser(int userId) {
    if (userId <= 0)
        return "";

    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db)
        return "";

    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "SELECT account_number FROM accounts "
        "WHERE user_id = ? AND status = 'active' LIMIT 1;";

    if (sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    ) != SQLITE_OK)
        return "";

    sqlite3_bind_int(statement, 1, userId);

    std::string accountNumber;

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* value = sqlite3_column_text(statement, 0);

        if (value)
            accountNumber = reinterpret_cast<const char*>(value);
    }

    sqlite3_finalize(statement);

    return accountNumber;
}

// ============================================================
// ACCOUNT BALANCE
// ============================================================

AccountBalance Database::getAccountBalance(int userId) {
    AccountBalance result;

    if (userId <= 0) {
        result.message = "Invalid user.";
        return result;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        result.message = "Database is not connected.";
        return result;
    }

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "SELECT account_number, balance_paise "
        "FROM accounts "
        "WHERE user_id = ? AND status = 'active' "
        "LIMIT 1;";

    if (sqlite3_prepare_v2(
        db, sql, -1, &stmt, nullptr
    ) != SQLITE_OK) {
        result.message = "Could not retrieve account balance.";
        return result;
    }

    sqlite3_bind_int(stmt, 1, userId);

    const int step = sqlite3_step(stmt);

    if (step == SQLITE_ROW) {
        const auto* account = sqlite3_column_text(stmt, 0);

        if (account)
            result.accountNumber =
                reinterpret_cast<const char*>(account);

        result.balancePaise =
            sqlite3_column_int64(stmt, 1);

        result.success = true;
        result.message = "Account balance retrieved successfully.";
    } else {
        result.message = "No active account found.";
    }

    sqlite3_finalize(stmt);

    return result;
}

// ============================================================
// DEPOSIT
// ============================================================

MoneyOperationResult Database::depositFunds(
    int userId,
    std::int64_t amountPaise
) {
    MoneyOperationResult result;

    if (userId <= 0 || amountPaise <= 0) {
        result.clientError = true;
        result.message = "Invalid deposit amount or user.";
        return result;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        result.message = "Database is not connected.";
        return result;
    }

    if (!executeSql(db, "BEGIN IMMEDIATE TRANSACTION;")) {
        result.message = "Could not start deposit transaction.";
        return result;
    }

    auto fail = [&](const std::string& message, bool clientError) {
        rollback(db);
        result.success = false;
        result.clientError = clientError;
        result.message = message;
        return result;
    };

    sqlite3_stmt* stmt = nullptr;

    const char* findSql =
        "SELECT id, balance_paise FROM accounts "
        "WHERE user_id = ? AND status = 'active' LIMIT 1;";

    if (sqlite3_prepare_v2(
        db, findSql, -1, &stmt, nullptr
    ) != SQLITE_OK)
        return fail("Could not retrieve account.", false);

    sqlite3_bind_int(stmt, 1, userId);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return fail("No active account found.", true);
    }

    const sqlite3_int64 accountId =
        sqlite3_column_int64(stmt, 0);

    const std::int64_t balance =
        sqlite3_column_int64(stmt, 1);

    sqlite3_finalize(stmt);

    if (balance >
        std::numeric_limits<std::int64_t>::max() - amountPaise)
        return fail("Account balance limit would be exceeded.", true);

    const char* updateSql =
        "UPDATE accounts SET balance_paise = balance_paise + ? "
        "WHERE id = ? AND status = 'active';";

    if (sqlite3_prepare_v2(
        db, updateSql, -1, &stmt, nullptr
    ) != SQLITE_OK)
        return fail("Could not prepare deposit.", false);

    sqlite3_bind_int64(stmt, 1, amountPaise);
    sqlite3_bind_int64(stmt, 2, accountId);

    const int updateStep = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db);

    sqlite3_finalize(stmt);

    if (updateStep != SQLITE_DONE || changed != 1)
        return fail("Deposit failed.", false);

    const char* ledgerSql =
        "INSERT INTO transactions "
        "(account_id, transaction_type, amount_paise, description) "
        "VALUES (?, 'deposit', ?, 'Cash deposit');";

    if (sqlite3_prepare_v2(
        db, ledgerSql, -1, &stmt, nullptr
    ) != SQLITE_OK)
        return fail("Could not prepare deposit ledger entry.", false);

    sqlite3_bind_int64(stmt, 1, accountId);
    sqlite3_bind_int64(stmt, 2, amountPaise);

    const int ledgerStep = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (ledgerStep != SQLITE_DONE)
        return fail("Could not record deposit.", false);

    if (!executeSql(db, "COMMIT;"))
        return fail("Could not commit deposit.", false);

    result.success = true;
    result.message = "Deposit completed successfully.";
    result.balancePaise = balance + amountPaise;

    return result;
}

// ============================================================
// WITHDRAWAL
// ============================================================

MoneyOperationResult Database::withdrawFunds(
    int userId,
    std::int64_t amountPaise
) {
    MoneyOperationResult result;

    if (userId <= 0 || amountPaise <= 0) {
        result.clientError = true;
        result.message = "Invalid withdrawal amount or user.";
        return result;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        result.message = "Database is not connected.";
        return result;
    }

    if (!executeSql(db, "BEGIN IMMEDIATE TRANSACTION;")) {
        result.message = "Could not start withdrawal transaction.";
        return result;
    }

    auto fail = [&](const std::string& message, bool clientError) {
        rollback(db);
        result.success = false;
        result.clientError = clientError;
        result.message = message;
        return result;
    };

    sqlite3_stmt* stmt = nullptr;

    const char* findSql =
        "SELECT id, balance_paise FROM accounts "
        "WHERE user_id = ? AND status = 'active' LIMIT 1;";

    if (sqlite3_prepare_v2(
        db, findSql, -1, &stmt, nullptr
    ) != SQLITE_OK)
        return fail("Could not retrieve account.", false);

    sqlite3_bind_int(stmt, 1, userId);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return fail("No active account found.", true);
    }

    const sqlite3_int64 accountId =
        sqlite3_column_int64(stmt, 0);

    const std::int64_t balance =
        sqlite3_column_int64(stmt, 1);

    sqlite3_finalize(stmt);

    if (balance < amountPaise)
        return fail("Insufficient balance.", true);

    const char* updateSql =
        "UPDATE accounts SET balance_paise = balance_paise - ? "
        "WHERE id = ? AND status = 'active' AND balance_paise >= ?;";

    if (sqlite3_prepare_v2(
        db, updateSql, -1, &stmt, nullptr
    ) != SQLITE_OK)
        return fail("Could not prepare withdrawal.", false);

    sqlite3_bind_int64(stmt, 1, amountPaise);
    sqlite3_bind_int64(stmt, 2, accountId);
    sqlite3_bind_int64(stmt, 3, amountPaise);

    const int updateStep = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db);

    sqlite3_finalize(stmt);

    if (updateStep != SQLITE_DONE || changed != 1)
        return fail("Withdrawal failed.", false);

    const char* ledgerSql =
        "INSERT INTO transactions "
        "(account_id, transaction_type, amount_paise, description) "
        "VALUES (?, 'withdrawal', ?, 'Cash withdrawal');";

    if (sqlite3_prepare_v2(
        db, ledgerSql, -1, &stmt, nullptr
    ) != SQLITE_OK)
        return fail("Could not prepare withdrawal ledger entry.", false);

    sqlite3_bind_int64(stmt, 1, accountId);
    sqlite3_bind_int64(stmt, 2, amountPaise);

    const int ledgerStep = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (ledgerStep != SQLITE_DONE)
        return fail("Could not record withdrawal.", false);

    if (!executeSql(db, "COMMIT;"))
        return fail("Could not commit withdrawal.", false);

    result.success = true;
    result.message = "Withdrawal completed successfully.";
    result.balancePaise = balance - amountPaise;

    return result;
}

// ============================================================
// TRANSACTION HISTORY
// ============================================================

TransactionHistory Database::getTransactionHistory(
    int userId,
    int limit,
    int offset
) {
    TransactionHistory result;

    if (userId <= 0 || limit <= 0 || offset < 0) {
        result.message = "Invalid transaction history request.";
        return result;
    }

    if (limit > 100)
        limit = 100;

    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        result.message = "Database is not connected.";
        return result;
    }

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "SELECT "
        "t.id, "
        "t.transaction_type, "
        "t.amount_paise, "
        "COALESCE(a.account_number, ''), "
        "COALESCE(t.description, ''), "
        "t.created_at "
        "FROM transactions t "
        "JOIN accounts own ON own.id = t.account_id "
        "LEFT JOIN accounts a ON a.id = t.related_account_id "
        "WHERE own.user_id = ? "
        "ORDER BY t.id DESC "
        "LIMIT ? OFFSET ?;";

    if (sqlite3_prepare_v2(
        db, sql, -1, &stmt, nullptr
    ) != SQLITE_OK) {
        result.message = "Could not retrieve transaction history.";
        return result;
    }

    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    int step;

    while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
        TransactionRecord record;

        record.id = sqlite3_column_int64(stmt, 0);

        const auto* type = sqlite3_column_text(stmt, 1);
        const auto* related = sqlite3_column_text(stmt, 3);
        const auto* description = sqlite3_column_text(stmt, 4);
        const auto* created = sqlite3_column_text(stmt, 5);

        record.transactionType =
            type ? reinterpret_cast<const char*>(type) : "";

        record.amountPaise =
            sqlite3_column_int64(stmt, 2);

        record.relatedAccountNumber =
            related ? reinterpret_cast<const char*>(related) : "";

        record.description =
            description ? reinterpret_cast<const char*>(description) : "";

        record.createdAt =
            created ? reinterpret_cast<const char*>(created) : "";

        result.transactions.push_back(record);
    }

    sqlite3_finalize(stmt);

    if (step != SQLITE_DONE) {
        result.transactions.clear();
        result.message = "Could not read transaction history.";
        return result;
    }

    result.success = true;
    result.message = "Transaction history retrieved successfully.";

    return result;
}

// ============================================================
// EXISTING TRANSFER FUNCTION
// ============================================================

TransferResult Database::transferFunds(
    const std::string& senderAcc,
    const std::string& receiverAcc,
    std::int64_t amountPaise
) {
    if (senderAcc.empty() || receiverAcc.empty())
        return {false, true, "Sender and receiver accounts are required.", 0};

    if (senderAcc == receiverAcc)
        return {false, true, "You cannot transfer money to your own account.", 0};

    if (amountPaise <= 0)
        return {false, true, "Transfer amount must be greater than zero.", 0};

    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db)
        return {false, false, "Database is not connected.", 0};

    if (!executeSql(db, "BEGIN IMMEDIATE TRANSACTION;"))
        return {false, false, "Could not start transfer transaction.", 0};

    auto fail = [this](const std::string& message, bool clientError,
                       std::int64_t balance = 0) {
        rollback(db);
        return TransferResult{false, clientError, message, balance};
    };

    struct AccountInfo {
        sqlite3_int64 id;
        std::int64_t balance;
    };

    auto loadAccount = [this](const std::string& number,
                              AccountInfo& info,
                              std::string& status) -> bool {
        sqlite3_stmt* stmt = nullptr;

        const char* sql =
            "SELECT id, balance_paise, status FROM accounts "
            "WHERE account_number = ?;";

        if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK)
            return false;

        sqlite3_bind_text(
            stmt, 1, number.c_str(), -1, SQLITE_TRANSIENT
        );

        const int step = sqlite3_step(stmt);

        if (step == SQLITE_ROW) {
            info.id = sqlite3_column_int64(stmt, 0);
            info.balance = sqlite3_column_int64(stmt, 1);

            const auto* text = sqlite3_column_text(stmt, 2);

            status = text
                ? reinterpret_cast<const char*>(text)
                : "";
        }

        sqlite3_finalize(stmt);

        return step == SQLITE_ROW;
    };

    AccountInfo sender{}, receiver;

    std::string senderStatus, receiverStatus;

    if (!loadAccount(senderAcc, sender, senderStatus))
        return fail("Sender account was not found.", true);

    if (senderStatus != "active")
        return fail("Sender account is blocked or closed.", true, sender.balance);

    if (!loadAccount(receiverAcc, receiver, receiverStatus))
        return fail("Receiver account was not found.", true, sender.balance);

    if (receiverStatus != "active")
        return fail("Receiver account is blocked or closed.", true, sender.balance);

    if (sender.balance < amountPaise)
        return fail("Insufficient balance.", true, sender.balance);

    if (receiver.balance >
        std::numeric_limits<std::int64_t>::max() - amountPaise)
        return fail("Receiver balance limit would be exceeded.", true, sender.balance);

    sqlite3_stmt* updateSender = nullptr;

    const char* debitSql =
        "UPDATE accounts SET balance_paise = balance_paise - ? "
        "WHERE id = ? AND status = 'active' AND balance_paise >= ?;";

    if (sqlite3_prepare_v2(
        db, debitSql, -1, &updateSender, nullptr
    ) != SQLITE_OK)
        return fail("Could not prepare sender debit.", false, sender.balance);

    sqlite3_bind_int64(updateSender, 1, amountPaise);
    sqlite3_bind_int64(updateSender, 2, sender.id);
    sqlite3_bind_int64(updateSender, 3, amountPaise);

    int step = sqlite3_step(updateSender);
    const int changedSender = sqlite3_changes(db);

    sqlite3_finalize(updateSender);

    if (step != SQLITE_DONE || changedSender != 1)
        return fail("Sender debit failed; transfer was cancelled.", true, sender.balance);

    sqlite3_stmt* updateReceiver = nullptr;

    const char* creditSql =
        "UPDATE accounts SET balance_paise = balance_paise + ? "
        "WHERE id = ? AND status = 'active';";

    if (sqlite3_prepare_v2(
        db, creditSql, -1, &updateReceiver, nullptr
    ) != SQLITE_OK)
        return fail("Could not prepare receiver credit.", false, sender.balance);

    sqlite3_bind_int64(updateReceiver, 1, amountPaise);
    sqlite3_bind_int64(updateReceiver, 2, receiver.id);

    step = sqlite3_step(updateReceiver);
    const int changedReceiver = sqlite3_changes(db);

    sqlite3_finalize(updateReceiver);

    if (step != SQLITE_DONE || changedReceiver != 1)
        return fail("Receiver credit failed; transfer was rolled back.", false, sender.balance);

    auto insertLedgerEntry = [this, amountPaise](
        sqlite3_int64 accountId,
        sqlite3_int64 relatedId,
        const char* type,
        const std::string& description
    ) {
        sqlite3_stmt* stmt = nullptr;

        const char* sql =
            "INSERT INTO transactions "
            "(account_id, transaction_type, amount_paise, related_account_id, description) "
            "VALUES (?, ?, ?, ?, ?);";

        if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, accountId);
        sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, amountPaise);
        sqlite3_bind_int64(stmt, 4, relatedId);

        sqlite3_bind_text(
            stmt, 5, description.c_str(), -1, SQLITE_TRANSIENT
        );

        const int result = sqlite3_step(stmt);

        sqlite3_finalize(stmt);

        return result == SQLITE_DONE;
    };

    const std::string outDescription =
        "Transfer to account " + receiverAcc;

    const std::string inDescription =
        "Transfer from account " + senderAcc;

    if (!insertLedgerEntry(
            sender.id, receiver.id, "transfer_out", outDescription
        ) ||
        !insertLedgerEntry(
            receiver.id, sender.id, "transfer_in", inDescription
        )) {
        return fail(
            "Could not record transfer; all changes were rolled back.",
            false,
            sender.balance
        );
    }

    if (!executeSql(db, "COMMIT;"))
        return fail(
            "Could not commit transfer; transaction was rolled back.",
            false,
            sender.balance
        );

    return {
        true,
        false,
        "Transfer completed successfully.",
        sender.balance - amountPaise
    };
}
