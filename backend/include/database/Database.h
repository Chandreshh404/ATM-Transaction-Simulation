#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <cstdint>
#include <mutex>
#include <string>

struct TransferResult {
    bool success = false;
    bool clientError = false;
    std::string message;
    std::int64_t senderBalancePaise = 0;
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

    // Returns the active account number belonging to this authenticated user.
    std::string getAccountNumberForUser(int userId);

    // amountPaise is an integer number of paise, never a floating-point rupee value.
    TransferResult transferFunds(
        const std::string& senderAcc,
        const std::string& receiverAcc,
        std::int64_t amountPaise
    );
};

#endif
