
#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct TransferResult {
    bool success = false;
    bool clientError = false;
    std::string message;
    std::int64_t senderBalancePaise = 0;
};

struct AccountBalance {
    bool success = false;
    std::string message;
    std::string accountNumber;
    std::int64_t balancePaise = 0;
};

struct TransactionRecord {
    std::int64_t id = 0;
    std::string transactionType;
    std::int64_t amountPaise = 0;
    std::string relatedAccountNumber;
    std::string description;
    std::string createdAt;
};

struct TransactionHistory {
    bool success = false;
    std::string message;
    std::vector<TransactionRecord> transactions;
};

struct MoneyOperationResult {
    bool success = false;
    bool clientError = false;
    std::string message;
    std::int64_t balancePaise = 0;
};

class Database {
private:
    sqlite3* db;
    std::mutex dbMutex;

public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const std::string& dbPath);
    void disconnect();

    bool execute(const std::string& sql);
    bool initializeSchema(const std::string& schemaPath);

    sqlite3* getConnection();
    std::mutex& getMutex();

    std::string getAccountNumberForUser(int userId);

    AccountBalance getAccountBalance(int userId);

    MoneyOperationResult depositFunds(
        int userId,
        std::int64_t amountPaise
    );

    MoneyOperationResult withdrawFunds(
        int userId,
        std::int64_t amountPaise
    );

    TransactionHistory getTransactionHistory(
        int userId,
        int limit,
        int offset
    );

    TransferResult transferFunds(
        const std::string& senderAcc,
        const std::string& receiverAcc,
        std::int64_t amountPaise
    );
};

#endif
