
#include "database/Database.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static int failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        std::cout << "[PASS] " << message << '\n';
    } else {
        std::cerr << "[FAIL] " << message << '\n';
        ++failures;
    }
}

int main() {
    const fs::path testDir =
        fs::temp_directory_path() / "atm_database_integration_test";

    std::error_code ec;
    fs::create_directories(testDir, ec);

    if (ec) {
        std::cerr << "Could not create test directory: "
                  << ec.message() << '\n';
        return 1;
    }

    const fs::path dbPath = testDir / "atm_test.db";
    const fs::path schemaPath =
        fs::path(ATM_PROJECT_SOURCE_DIR) / "database" / "schema.sql";

    fs::remove(dbPath, ec);

    Database db;

    if (!db.connect(dbPath.string())) {
        std::cerr << "[FAIL] Database connection failed.\n";
        return 1;
    }

    check(true, "Database connection");

    check(
        db.initializeSchema(schemaPath.string()),
        "Database schema initialization"
    );

    // Create two independent test users and accounts.
    const bool seedSuccess = db.execute(
        "INSERT INTO users (full_name, email, pin_hash) VALUES "
        "('Test User One', 'test1@example.com', 'test_hash_1'),"
        "('Test User Two', 'test2@example.com', 'test_hash_2');"
        "INSERT INTO accounts (user_id, account_number, balance_paise) VALUES "
        "(1, '900000000001', 100000),"
        "(2, '900000000002', 50000);"
    );

    check(seedSuccess, "Create test users and accounts");

    if (!seedSuccess) {
        db.disconnect();
        return 1;
    }

    // Account lookup and initial balances.
    check(
        db.getAccountNumberForUser(1) == "900000000001",
        "Retrieve account number"
    );

    AccountBalance initial = db.getAccountBalance(1);

    check(
        initial.success && initial.balancePaise == 100000,
        "Retrieve initial account balance"
    );

    // Deposit ₹100.
    MoneyOperationResult deposit = db.depositFunds(1, 10000);

    check(
        deposit.success && deposit.balancePaise == 110000,
        "Deposit ₹100"
    );

    // Withdraw ₹30.
    MoneyOperationResult withdrawal = db.withdrawFunds(1, 3000);

    check(
        withdrawal.success && withdrawal.balancePaise == 107000,
        "Withdraw ₹30"
    );

    // Reject an overdraft.
    MoneyOperationResult overdraft = db.withdrawFunds(1, 200000);

    check(
        !overdraft.success && overdraft.clientError,
        "Reject withdrawal with insufficient funds"
    );

    // Reject invalid amounts.
    MoneyOperationResult invalidDeposit = db.depositFunds(1, 0);

    check(
        !invalidDeposit.success,
        "Reject zero-value deposit"
    );

    // Transfer ₹20 from account one to account two.
    TransferResult transfer = db.transferFunds(
        "900000000001",
        "900000000002",
        2000
    );

    check(
        transfer.success && transfer.senderBalancePaise == 105000,
        "Transfer ₹20 between accounts"
    );

    AccountBalance sender = db.getAccountBalance(1);
    AccountBalance receiver = db.getAccountBalance(2);

    check(
        sender.success && sender.balancePaise == 105000,
        "Verify sender balance after transfer"
    );

    check(
        receiver.success && receiver.balancePaise == 52000,
        "Verify receiver balance after transfer"
    );

    // Verify transaction history.
    TransactionHistory history = db.getTransactionHistory(1, 20, 0);

    check(
        history.success && history.transactions.size() == 3,
        "Retrieve transaction history"
    );

    if (history.success && !history.transactions.empty()) {
        check(
            history.transactions.front().transactionType == "transfer_out",
            "Transaction history is newest first"
        );
    }

    // Test concurrent withdrawals.
    // Account one currently has ₹1050.
    std::atomic<int> successfulWithdrawals{0};
    std::vector<std::thread> workers;

    for (int i = 0; i < 10; ++i) {
        workers.emplace_back([&]() {
            MoneyOperationResult result = db.withdrawFunds(1, 20000);

            if (result.success) {
                ++successfulWithdrawals;
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    AccountBalance afterConcurrent = db.getAccountBalance(1);

    check(
        successfulWithdrawals == 5,
        "Concurrent withdrawals allow exactly five successful withdrawals"
    );

    check(
        afterConcurrent.success && afterConcurrent.balancePaise == 5000,
        "Concurrent withdrawals preserve correct final balance"
    );

    // Verify that all successful withdrawals have ledger entries.
    TransactionHistory finalHistory = db.getTransactionHistory(1, 100, 0);

    check(
        finalHistory.success && finalHistory.transactions.size() == 8,
        "Ledger contains all successful account transactions"
    );

    db.disconnect();

    // Remove the isolated test database.
    fs::remove(dbPath, ec);

    if (failures == 0) {
        std::cout << "\nAll database integration tests passed.\n";
        fs::remove(testDir, ec);
        return 0;
    }

    std::cerr << "\nDatabase integration tests failed: "
              << failures << '\n';

    return 1;
}
