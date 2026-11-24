
'use strict';

const API_BASE_URL = window.ATM_API_BASE_URL || 'http://127.0.0.1:8080';

// Session is kept in memory, not localStorage.
let sessionToken = null;
let accountNumber = null;
let userId = null;

// HTML views
const loginView = document.getElementById('loginView');
const registerView = document.getElementById('registerView');
const dashboardView = document.getElementById('dashboardView');

// Authentication forms
const loginForm = document.getElementById('loginForm');
const registerForm = document.getElementById('registerForm');

// Navigation buttons
const showRegisterButton = document.getElementById('showRegister');
const showLoginButton = document.getElementById('showLogin');
const logoutButton = document.getElementById('logoutButton');

// Dashboard elements
const dashboardBalance = document.getElementById('dashboardBalance');
const dashboardAccount = document.getElementById('dashboardAccount');
const dashboardName = document.getElementById('dashboardName');
const dashboardStatus = document.getElementById('dashboardStatus');

// Operation forms
const depositForm = document.getElementById('depositForm');
const withdrawForm = document.getElementById('withdrawForm');
const transferForm = document.getElementById('transferForm');

// Transaction history
const transactionTableBody =
    document.getElementById('transactionTableBody');

// --------------------------------------------------
// GENERAL HELPERS
// --------------------------------------------------

function formatRupees(value) {
    return new Intl.NumberFormat('en-IN', {
        style: 'currency',
        currency: 'INR',
        minimumFractionDigits: 2
    }).format(value);
}

function showStatus(elementId, message, type = '') {
    const element = document.getElementById(elementId);

    element.textContent = message;
    element.className = 'status';

    if (type) {
        element.classList.add(type);
    }
}

function clearStatuses() {
    [
        'loginStatus',
        'registerStatus',
        'dashboardStatus',
        'depositStatus',
        'withdrawStatus',
        'transferStatus',
        'historyStatus'
    ].forEach(id => showStatus(id, ''));
}

function showMainView(view) {
    loginView.classList.add('hidden');
    registerView.classList.add('hidden');
    dashboardView.classList.add('hidden');

    view.classList.remove('hidden');
}

function showDashboardSection(sectionId) {
    document.querySelectorAll('.content-panel').forEach(section => {
        section.classList.add('hidden');
    });

    if (sectionId) {
        document.getElementById(sectionId).classList.remove('hidden');
    }
}

function requireSession() {
    if (!sessionToken) {
        logout(false);
        return false;
    }

    return true;
}

// --------------------------------------------------
// API HELPER
// --------------------------------------------------

async function apiRequest(endpoint, options = {}) {
    const headers = {
        ...(options.headers || {})
    };

    if (options.body !== undefined) {
        headers['Content-Type'] = 'application/json';
    }

    if (sessionToken) {
        headers['Authorization'] = `Bearer ${sessionToken}`;
    }

    let response;

    try {
        response = await fetch(`${API_BASE_URL}${endpoint}`, {
            ...options,
            headers
        });
    } catch (error) {
        throw new Error(
            'Cannot connect to ATM backend. Check whether the server is running.'
        );
    }

    const result = await response.json().catch(() => ({}));

    if (response.status === 401) {
        logout(false);
        throw new Error('Session expired or invalid. Please log in again.');
    }

    if (!response.ok) {
        throw new Error(
            result.message || `Request failed (HTTP ${response.status}).`
        );
    }

    return result;
}

// --------------------------------------------------
// LOGIN
// --------------------------------------------------

loginForm.addEventListener('submit', async event => {
    event.preventDefault();

    const button = loginForm.querySelector('button[type="submit"]');
    button.disabled = true;
    button.textContent = 'Logging in...';

    showStatus('loginStatus', '');

    const email = document.getElementById('loginEmail').value.trim();
    const pin = document.getElementById('loginPin').value;

    try {
        const result = await apiRequest('/api/auth/login', {
            method: 'POST',
            body: JSON.stringify({ email, pin })
        });

        if (!result.sessionToken) {
            throw new Error('Login response did not contain a session token.');
        }

        sessionToken = result.sessionToken;
        accountNumber = result.accountNumber;
        userId = result.userId;

        dashboardAccount.textContent = accountNumber;
        dashboardName.textContent = `User ID: ${userId}`;

        loginForm.reset();

        showMainView(dashboardView);
        showDashboardSection(null);

        await refreshBalance();

        showStatus(
            'dashboardStatus',
            'Login successful.',
            'success'
        );

    } catch (error) {
        showStatus('loginStatus', error.message, 'error');
    } finally {
        button.disabled = false;
        button.textContent = 'Login';
    }
});

// --------------------------------------------------
// REGISTRATION
// --------------------------------------------------

registerForm.addEventListener('submit', async event => {
    event.preventDefault();

    const button = registerForm.querySelector('button[type="submit"]');
    button.disabled = true;
    button.textContent = 'Creating account...';

    showStatus('registerStatus', '');

    const fullName =
        document.getElementById('registerName').value.trim();

    const email =
        document.getElementById('registerEmail').value.trim();

    const pin =
        document.getElementById('registerPin').value;

    try {
        const result = await apiRequest('/api/auth/register', {
            method: 'POST',
            body: JSON.stringify({ fullName, email, pin })
        });

        registerForm.reset();

        showStatus(
            'registerStatus',
            `${result.message} Your account number is ${result.accountNumber}. You can now log in.`,
            'success'
        );

    } catch (error) {
        showStatus('registerStatus', error.message, 'error');
    } finally {
        button.disabled = false;
        button.textContent = 'Create account';
    }
});

// --------------------------------------------------
// SWITCH LOGIN / REGISTRATION
// --------------------------------------------------

showRegisterButton.addEventListener('click', () => {
    clearStatuses();
    showMainView(registerView);
});

showLoginButton.addEventListener('click', () => {
    clearStatuses();
    showMainView(loginView);
});

// --------------------------------------------------
// ACCOUNT BALANCE
// --------------------------------------------------

async function refreshBalance() {
    if (!requireSession()) return;

    try {
        const result = await apiRequest('/api/account/balance');

        dashboardBalance.textContent = formatRupees(result.balance);
        dashboardAccount.textContent = result.accountNumber;

    } catch (error) {
        showStatus('dashboardStatus', error.message, 'error');
    }
}

document.getElementById('refreshBalance').addEventListener(
    'click',
    refreshBalance
);

// --------------------------------------------------
// DASHBOARD NAVIGATION
// --------------------------------------------------

document.querySelectorAll('[data-view]').forEach(button => {
    button.addEventListener('click', async () => {
        if (!requireSession()) return;

        clearStatuses();

        const sectionId = button.dataset.view;
        showDashboardSection(sectionId);

        if (sectionId === 'historyView') {
            await loadTransactionHistory();
        }
    });
});

document.querySelectorAll('[data-back]').forEach(button => {
    button.addEventListener('click', () => {
        clearStatuses();
        showDashboardSection(null);
    });
});

// --------------------------------------------------
// AMOUNT VALIDATION
// --------------------------------------------------

function validateAmount(value) {
    const amount = Number(value);

    if (
        !Number.isFinite(amount) ||
        amount <= 0 ||
        Math.round(amount * 100) !== amount * 100
    ) {
        throw new Error(
            'Enter a positive amount with no more than two decimal places.'
        );
    }

    return amount;
}

// --------------------------------------------------
// DEPOSIT
// --------------------------------------------------

depositForm.addEventListener('submit', async event => {
    event.preventDefault();

    if (!requireSession()) return;

    const button = depositForm.querySelector('button[type="submit"]');
    button.disabled = true;
    button.textContent = 'Processing...';

    showStatus('depositStatus', '');

    try {
        const amount = validateAmount(
            document.getElementById('depositAmount').value
        );

        const result = await apiRequest('/api/deposit', {
            method: 'POST',
            body: JSON.stringify({ amount })
        });

        depositForm.reset();

        dashboardBalance.textContent = formatRupees(result.balance);

        showStatus(
            'depositStatus',
            result.message,
            'success'
        );

    } catch (error) {
        showStatus('depositStatus', error.message, 'error');
    } finally {
        button.disabled = false;
        button.textContent = 'Deposit';
    }
});

// --------------------------------------------------
// WITHDRAWAL
// --------------------------------------------------

withdrawForm.addEventListener('submit', async event => {
    event.preventDefault();

    if (!requireSession()) return;

    const button = withdrawForm.querySelector('button[type="submit"]');
    button.disabled = true;
    button.textContent = 'Processing...';

    showStatus('withdrawStatus', '');

    try {
        const amount = validateAmount(
            document.getElementById('withdrawAmount').value
        );

        const result = await apiRequest('/api/withdraw', {
            method: 'POST',
            body: JSON.stringify({ amount })
        });

        withdrawForm.reset();

        dashboardBalance.textContent = formatRupees(result.balance);

        showStatus(
            'withdrawStatus',
            result.message,
            'success'
        );

    } catch (error) {
        showStatus('withdrawStatus', error.message, 'error');
    } finally {
        button.disabled = false;
        button.textContent = 'Withdraw';
    }
});

// --------------------------------------------------
// FUND TRANSFER
// --------------------------------------------------

transferForm.addEventListener('submit', async event => {
    event.preventDefault();

    if (!requireSession()) return;

    const button = transferForm.querySelector('button[type="submit"]');
    button.disabled = true;
    button.textContent = 'Processing...';

    showStatus('transferStatus', '');

    try {
        const targetAccount =
            document.getElementById('targetAccount').value.trim();

        if (!/^\d+$/.test(targetAccount)) {
            throw new Error('Enter a valid numeric receiver account number.');
        }

        const amount = validateAmount(
            document.getElementById('transferAmount').value
        );

        const result = await apiRequest('/api/transfer', {
            method: 'POST',
            body: JSON.stringify({ targetAccount, amount })
        });

        transferForm.reset();

        dashboardBalance.textContent = formatRupees(result.balance);

        showStatus(
            'transferStatus',
            result.message,
            'success'
        );

    } catch (error) {
        showStatus('transferStatus', error.message, 'error');
    } finally {
        button.disabled = false;
        button.textContent = 'Transfer';
    }
});

// --------------------------------------------------
// TRANSACTION HISTORY
// --------------------------------------------------

async function loadTransactionHistory() {
    if (!requireSession()) return;

    const tableBody = transactionTableBody;

    tableBody.replaceChildren();

    const loadingRow = document.createElement('tr');
    const loadingCell = document.createElement('td');

    loadingCell.colSpan = 4;
    loadingCell.textContent = 'Loading transactions...';
    loadingRow.appendChild(loadingCell);
    tableBody.appendChild(loadingRow);

    try {
        const result = await apiRequest(
            '/api/transactions?limit=20&offset=0'
        );

        tableBody.replaceChildren();

        if (!result.transactions || result.transactions.length === 0) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');

            cell.colSpan = 4;
            cell.textContent = 'No transactions found.';

            row.appendChild(cell);
            tableBody.appendChild(row);
            return;
        }

        result.transactions.forEach(transaction => {
            const row = document.createElement('tr');

            const typeCell = document.createElement('td');
            const amountCell = document.createElement('td');
            const accountCell = document.createElement('td');
            const dateCell = document.createElement('td');

            typeCell.textContent = transaction.transactionType
                .replace('_', ' ')
                .toUpperCase();

            const isCredit = [
                'deposit',
                'transfer_in'
            ].includes(transaction.transactionType);

            amountCell.textContent =
                `${isCredit ? '+' : '-'}${formatRupees(transaction.amount)}`;

            amountCell.className = isCredit
                ? 'transaction-credit'
                : 'transaction-debit';

            accountCell.textContent =
                transaction.relatedAccountNumber || '—';

            const parsedDate = new Date(
                transaction.createdAt.replace(' ', 'T') + 'Z'
            );

            dateCell.textContent = Number.isNaN(parsedDate.getTime())
                ? transaction.createdAt
                : parsedDate.toLocaleString('en-IN');

            row.append(
                typeCell,
                amountCell,
                accountCell,
                dateCell
            );

            tableBody.appendChild(row);
        });

        showStatus(
            'historyStatus',
            'Transaction history loaded.',
            'success'
        );

    } catch (error) {
        tableBody.replaceChildren();

        const row = document.createElement('tr');
        const cell = document.createElement('td');

        cell.colSpan = 4;
        cell.textContent = error.message;

        row.appendChild(cell);
        tableBody.appendChild(row);

        showStatus('historyStatus', error.message, 'error');
    }
}

document.getElementById('loadHistory').addEventListener(
    'click',
    loadTransactionHistory
);

// --------------------------------------------------
// LOGOUT
// --------------------------------------------------

async function logout(callBackend = true) {
    const oldToken = sessionToken;

    sessionToken = null;
    accountNumber = null;
    userId = null;

    dashboardBalance.textContent = '₹0.00';
    dashboardAccount.textContent = '—';
    dashboardName.textContent = '';

    clearStatuses();

    showDashboardSection(null);
    showMainView(loginView);

    if (callBackend && oldToken) {
        try {
            await fetch(`${API_BASE_URL}/api/auth/logout`, {
                method: 'POST',
                headers: {
                    'Authorization': `Bearer ${oldToken}`
                }
            });
        } catch (error) {
            console.error('Logout request failed:', error);
        }
    }
}

logoutButton.addEventListener('click', () => logout(true));

// --------------------------------------------------
// INITIAL APPLICATION STATE
// --------------------------------------------------

showMainView(loginView);
