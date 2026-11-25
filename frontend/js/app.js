 'use strict';

const API_BASE_URL = window.ATM_API_BASE_URL || 'http://127.0.0.1:8080';

// Light/dark theme preference (stored only in this browser).
const themeToggle = document.getElementById('themeToggle');
const themeIcon = themeToggle.querySelector('.theme-toggle-icon');
const themeLabel = themeToggle.querySelector('.theme-toggle-label');
function applyTheme(theme) {
  const dark = theme === 'dark';
  document.documentElement.dataset.theme = dark ? 'dark' : 'light';
  themeIcon.textContent = dark ? '☀' : '☾';
  themeLabel.textContent = dark ? 'Light mode' : 'Dark mode';
  themeToggle.setAttribute('aria-label', dark ? 'Switch to light mode' : 'Switch to dark mode');
}
let savedTheme = 'light';
try { savedTheme = localStorage.getItem('nexusBankTheme') || 'light'; } catch (_) {}
applyTheme(savedTheme);
themeToggle.addEventListener('click', () => {
  const nextTheme = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
  applyTheme(nextTheme);
  try { localStorage.setItem('nexusBankTheme', nextTheme); } catch (_) {}
});


let sessionToken = null;
let accountNumber = null;
let userId = null;
let customerName = '';

const loginView = document.getElementById('loginView');
const registerView = document.getElementById('registerView');
const dashboardView = document.getElementById('dashboardView');
const loginForm = document.getElementById('loginForm');
const registerForm = document.getElementById('registerForm');
const showRegisterButton = document.getElementById('showRegister');
const showLoginButton = document.getElementById('showLogin');
const logoutButton = document.getElementById('logoutButton');
const dashboardBalance = document.getElementById('dashboardBalance');
const dashboardAccount = document.getElementById('dashboardAccount');
const dashboardName = document.getElementById('dashboardName');
const dashboardStatus = document.getElementById('dashboardStatus');
const depositForm = document.getElementById('depositForm');
const withdrawForm = document.getElementById('withdrawForm');
const transferForm = document.getElementById('transferForm');
const transactionTableBody = document.getElementById('transactionTableBody');

function formatRupees(value) {
  return new Intl.NumberFormat('en-IN', { style: 'currency', currency: 'INR', minimumFractionDigits: 2 }).format(Number(value));
}
function showStatus(elementId, message, type = '') {
  const element = document.getElementById(elementId);
  if (!element) return;
  element.textContent = message;
  element.className = 'status';
  if (type) element.classList.add(type);
}
function clearStatuses() {
  ['loginStatus','registerStatus','dashboardStatus','depositStatus','withdrawStatus','transferStatus','historyStatus']
    .forEach(id => showStatus(id, ''));
}
function showMainView(view) {
  [loginView, registerView, dashboardView].forEach(item => item.classList.add('hidden'));
  view.classList.remove('hidden');
}
function setActiveNavigation(sectionId) {
  document.querySelectorAll('.nav-item').forEach(button => {
    button.classList.toggle('active', button.dataset.view === sectionId);
  });
}
function showDashboardSection(sectionId) {
  document.querySelectorAll('.page-view').forEach(section => section.classList.add('hidden'));
  const target = document.getElementById(sectionId || 'overviewView');
  if (target) target.classList.remove('hidden');
  setActiveNavigation(sectionId || 'overviewView');
}
function requireSession() {
  if (!sessionToken) { logout(false); return false; }
  return true;
}
// Free Render backends sleep after ~15 minutes idle and take up to a minute to
// wake. Give the first request time and retry instead of failing immediately.
const REQUEST_TIMEOUT_MS = 70000;
const WAKE_RETRIES = 2;

function fetchWithTimeout(url, options, timeoutMs = REQUEST_TIMEOUT_MS) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  return fetch(url, { ...options, signal: controller.signal }).finally(() => clearTimeout(timer));
}

// Ping the backend as soon as the page opens so it is already awake when the user signs in.
function wakeBackend() {
  fetchWithTimeout(`${API_BASE_URL}/api/health`, {}, REQUEST_TIMEOUT_MS).catch(() => {});
}
wakeBackend();

async function apiRequest(endpoint, options = {}, statusId = null) {
  const headers = { ...(options.headers || {}) };
  if (options.body !== undefined) headers['Content-Type'] = 'application/json';
  if (sessionToken) headers.Authorization = `Bearer ${sessionToken}`;
  const isRead = !options.method || options.method === 'GET';
  let response;
  for (let attempt = 0; ; attempt++) {
    try {
      response = await fetchWithTimeout(`${API_BASE_URL}${endpoint}`, { ...options, headers });
      break;
    } catch (_) {
      // Only auto-retry reads and logins; never replay a money movement that may have succeeded.
      const safeToRetry = isRead || endpoint === '/api/auth/login' || endpoint === '/api/auth/register';
      if (!safeToRetry || attempt >= WAKE_RETRIES) {
        throw new Error('Cannot connect to the ATM backend. The free server may be waking up - please try again in a minute.');
      }
      if (statusId) showStatus(statusId, 'Waking up the server, this can take up to a minute…');
      await new Promise(resolve => setTimeout(resolve, 3000));
    }
  }
  const result = await response.json().catch(() => ({}));
  if (response.status === 401 && endpoint !== '/api/auth/login') {
    logout(false);
    throw new Error('Your session has expired. Please sign in again.');
  }
  if (!response.ok) throw new Error(result.message || `Request failed (HTTP ${response.status}).`);
  return result;
}
function setBusy(button, busyText, isBusy) {
  if (!button.dataset.originalText) button.dataset.originalText = button.textContent;
  button.disabled = isBusy;
  button.textContent = isBusy ? busyText : button.dataset.originalText;
}
function setCustomerIdentity(name) {
  customerName = name || 'Customer';
  const first = customerName.trim().split(/\s+/)[0] || 'Customer';
  document.getElementById('welcomeName').textContent = first;
  document.getElementById('topUserName').textContent = customerName;
  document.getElementById('userInitial').textContent = first.charAt(0).toUpperCase();
}
function safeDate(value) {
  if (!value) return '—';
  const parsed = new Date(String(value).replace(' ', 'T') + (String(value).endsWith('Z') ? '' : 'Z'));
  return Number.isNaN(parsed.getTime()) ? String(value) : parsed.toLocaleString('en-IN');
}
function buildTransactionRow(transaction) {
  const row = document.createElement('tr');
  const typeCell = document.createElement('td');
  const relatedCell = document.createElement('td');
  const dateCell = document.createElement('td');
  const amountCell = document.createElement('td');
  const type = String(transaction.transactionType || 'transaction');
  const isCredit = ['deposit', 'transfer_in'].includes(type);
  typeCell.textContent = type.replaceAll('_', ' ').replace(/\b\w/g, c => c.toUpperCase());
  relatedCell.textContent = transaction.relatedAccountNumber || '—';
  dateCell.textContent = safeDate(transaction.createdAt);
  amountCell.textContent = `${isCredit ? '+' : '−'}${formatRupees(transaction.amount)}`;
  amountCell.className = isCredit ? 'transaction-credit' : 'transaction-debit align-right';
  row.append(typeCell, relatedCell, dateCell, amountCell);
  return row;
}

// Login
loginForm.addEventListener('submit', async event => {
  event.preventDefault();
  const button = loginForm.querySelector('button[type="submit"]');
  setBusy(button, 'Signing in…', true);
  showStatus('loginStatus', '');
  const email = document.getElementById('loginEmail').value.trim();
  const pin = document.getElementById('loginPin').value;
  try {
    const result = await apiRequest('/api/auth/login', { method: 'POST', body: JSON.stringify({ email, pin }) }, 'loginStatus');
    if (!result.sessionToken) throw new Error('Login response did not contain a session token.');
    sessionToken = result.sessionToken;
    accountNumber = result.accountNumber;
    userId = result.userId;
    setCustomerIdentity(result.fullName || email.split('@')[0]);
    dashboardAccount.textContent = accountNumber || '—';
    dashboardName.textContent = userId ?? '—';
    loginForm.reset();
    showMainView(dashboardView);
    showDashboardSection('overviewView');
    await refreshBalance();
    await loadRecentTransactions();
    showStatus('dashboardStatus', 'You are signed in. Welcome to your account.', 'success');
  } catch (error) {
    showStatus('loginStatus', error.message, 'error');
  } finally {
    setBusy(button, '', false);
  }
});

// Registration
registerForm.addEventListener('submit', async event => {
  event.preventDefault();
  const button = registerForm.querySelector('button[type="submit"]');
  setBusy(button, 'Creating account…', true);
  showStatus('registerStatus', '');
  const fullName = document.getElementById('registerName').value.trim();
  const email = document.getElementById('registerEmail').value.trim();
  const pin = document.getElementById('registerPin').value;
  try {
    const result = await apiRequest('/api/auth/register', { method: 'POST', body: JSON.stringify({ fullName, email, pin }) }, 'registerStatus');
    registerForm.reset();
    showStatus('registerStatus', `${result.message} Your account number is ${result.accountNumber}. You can now sign in.`, 'success');
  } catch (error) {
    showStatus('registerStatus', error.message, 'error');
  } finally {
    setBusy(button, '', false);
  }
});

showRegisterButton.addEventListener('click', () => { clearStatuses(); showMainView(registerView); });
showLoginButton.addEventListener('click', () => { clearStatuses(); showMainView(loginView); });

async function refreshBalance() {
  if (!requireSession()) return;
  const button = document.getElementById('refreshBalance');
  if (button) setBusy(button, 'Refreshing…', true);
  try {
    const result = await apiRequest('/api/account/balance');
    dashboardBalance.textContent = formatRupees(result.balance);
    dashboardAccount.textContent = result.accountNumber || accountNumber || '—';
  } catch (error) {
    showStatus('dashboardStatus', error.message, 'error');
  } finally {
    if (button) setBusy(button, '', false);
  }
}
document.getElementById('refreshBalance').addEventListener('click', refreshBalance);

async function loadRecentTransactions() {
  const body = document.getElementById('recentTransactionBody');
  if (!body || !sessionToken) return;
  body.replaceChildren();
  try {
    const result = await apiRequest('/api/transactions?limit=5&offset=0');
    if (!result.transactions?.length) {
      const row = document.createElement('tr');
      const cell = document.createElement('td');
      cell.colSpan = 4; cell.className = 'empty-cell'; cell.textContent = 'No transactions yet. Your account activity will appear here.';
      row.appendChild(cell); body.appendChild(row); return;
    }
    result.transactions.slice(0, 5).forEach(item => body.appendChild(buildTransactionRow(item)));
  } catch (error) {
    const row = document.createElement('tr');
    const cell = document.createElement('td');
    cell.colSpan = 4; cell.className = 'empty-cell'; cell.textContent = error.message;
    row.appendChild(cell); body.appendChild(row);
  }
}

document.querySelectorAll('[data-view]').forEach(button => {
  button.addEventListener('click', async () => {
    if (!requireSession()) return;
    clearStatuses();
    const sectionId = button.dataset.view;
    showDashboardSection(sectionId);
    if (sectionId === 'historyView') await loadTransactionHistory();
  });
});
document.querySelectorAll('[data-back]').forEach(button => button.addEventListener('click', () => {
  clearStatuses(); showDashboardSection('overviewView');
}));

function validateAmount(value) {
  const amount = Number(value);
  if (!Number.isFinite(amount) || amount <= 0 || Math.round(amount * 100) !== amount * 100) {
    throw new Error('Enter a positive amount with no more than two decimal places.');
  }
  return amount;
}

async function submitMoneyOperation({ form, statusId, endpoint, busyText, buttonText, payload }) {
  if (!requireSession()) return;
  const button = form.querySelector('button[type="submit"]');
  setBusy(button, busyText, true);
  showStatus(statusId, '');
  try {
    const result = await apiRequest(endpoint, { method: 'POST', body: JSON.stringify(payload()) });
    form.reset();
    if (result.balance !== undefined) dashboardBalance.textContent = formatRupees(result.balance);
    showStatus(statusId, result.message || 'Transaction completed successfully.', 'success');
    await loadRecentTransactions();
  } catch (error) {
    showStatus(statusId, error.message, 'error');
  } finally {
    setBusy(button, '', false);
  }
}
depositForm.addEventListener('submit', event => {
  event.preventDefault();
  submitMoneyOperation({
    form: depositForm, statusId: 'depositStatus', endpoint: '/api/deposit',
    busyText: 'Processing…', payload: () => ({ amount: validateAmount(document.getElementById('depositAmount').value) })
  });
});
withdrawForm.addEventListener('submit', event => {
  event.preventDefault();
  submitMoneyOperation({
    form: withdrawForm, statusId: 'withdrawStatus', endpoint: '/api/withdraw',
    busyText: 'Processing…', payload: () => ({ amount: validateAmount(document.getElementById('withdrawAmount').value) })
  });
});
transferForm.addEventListener('submit', event => {
  event.preventDefault();
  submitMoneyOperation({
    form: transferForm, statusId: 'transferStatus', endpoint: '/api/transfer',
    busyText: 'Processing…', payload: () => {
      const targetAccount = document.getElementById('targetAccount').value.trim();
      if (!/^\d+$/.test(targetAccount)) throw new Error('Enter a valid numeric receiver account number.');
      return { targetAccount, amount: validateAmount(document.getElementById('transferAmount').value) };
    }
  });
});

async function loadTransactionHistory() {
  if (!requireSession()) return;
  transactionTableBody.replaceChildren();
  const loading = document.createElement('tr');
  const loadingCell = document.createElement('td');
  loadingCell.colSpan = 4; loadingCell.className = 'empty-cell'; loadingCell.textContent = 'Loading transactions…';
  loading.appendChild(loadingCell); transactionTableBody.appendChild(loading);
  try {
    const result = await apiRequest('/api/transactions?limit=50&offset=0');
    transactionTableBody.replaceChildren();
    if (!result.transactions?.length) {
      const row = document.createElement('tr');
      const cell = document.createElement('td');
      cell.colSpan = 4; cell.className = 'empty-cell'; cell.textContent = 'No transactions found.';
      row.appendChild(cell); transactionTableBody.appendChild(row);
    } else {
      result.transactions.forEach(transaction => transactionTableBody.appendChild(buildTransactionRow(transaction)));
    }
    showStatus('historyStatus', 'Transaction history loaded.', 'success');
  } catch (error) {
    transactionTableBody.replaceChildren();
    const row = document.createElement('tr');
    const cell = document.createElement('td');
    cell.colSpan = 4; cell.className = 'empty-cell'; cell.textContent = error.message;
    row.appendChild(cell); transactionTableBody.appendChild(row);
    showStatus('historyStatus', error.message, 'error');
  }
}
document.getElementById('loadHistory').addEventListener('click', loadTransactionHistory);

async function logout(callBackend = true) {
  const oldToken = sessionToken;
  sessionToken = null; accountNumber = null; userId = null; customerName = '';
  dashboardBalance.textContent = '₹0.00';
  dashboardAccount.textContent = '—';
  dashboardName.textContent = '—';
  document.getElementById('recentTransactionBody').replaceChildren();
  clearStatuses();
  showMainView(loginView);
  if (callBackend && oldToken) {
    try {
      await fetchWithTimeout(`${API_BASE_URL}/api/auth/logout`, { method: 'POST', headers: { Authorization: `Bearer ${oldToken}` } });
    } catch (error) { console.error('Logout request failed:', error); }
  }
}
logoutButton.addEventListener('click', () => logout(true));
showMainView(loginView);
