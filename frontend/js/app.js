'use strict';

const API_BASE_URL = 'http://127.0.0.1:8080';

const form = document.getElementById('transferForm');
const tokenInput = document.getElementById('sessionToken');
const targetInput = document.getElementById('targetAccount');
const amountInput = document.getElementById('transferAmount');
const submitButton = document.getElementById('transferButton');
const statusElement = document.getElementById('transferStatus');
const balanceElement = document.getElementById('currentBalance');

// Reuse a token saved by a login page if one exists. The field remains editable
// so this transfer page can also be tested independently.
try {
    tokenInput.value = localStorage.getItem('atmSessionToken') || '';
} catch (_) {
    // Storage may be unavailable in some browser privacy modes.
}

tokenInput.addEventListener('change', () => {
    try {
        localStorage.setItem('atmSessionToken', tokenInput.value.trim());
    } catch (_) {
        // The token can still be used for this page session.
    }
});

function showStatus(message, type) {
    statusElement.textContent = message;
    statusElement.className = type;
}

function formatRupees(value) {
    return new Intl.NumberFormat('en-IN', {
        style: 'currency',
        currency: 'INR',
        minimumFractionDigits: 2
    }).format(value);
}

form.addEventListener('submit', async (event) => {
    event.preventDefault();
    showStatus('', '');

    const sessionToken = tokenInput.value.trim();
    const targetAcc = targetInput.value.trim();
    const amount = Number(amountInput.value);

    if (!sessionToken) {
        showStatus('Enter your session token. Log in first if you do not have one.', 'error');
        return;
    }
    if (!/^\d+$/.test(targetAcc)) {
        showStatus('Enter a valid numeric receiver account number.', 'error');
        return;
    }
    if (!Number.isFinite(amount) || amount <= 0 || Math.round(amount * 100) !== amount * 100) {
        showStatus('Enter a positive amount with no more than two decimal places.', 'error');
        return;
    }

    submitButton.disabled = true;
    submitButton.textContent = 'Processing…';

    try {
        const response = await fetch(`${API_BASE_URL}/api/transfer`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${sessionToken}`
            },
            body: JSON.stringify({ targetAccount: targetAcc, amount })
        });

        const result = await response.json().catch(() => ({}));
        if (!response.ok) {
            if (response.status === 401) {
                showStatus(result.message || 'Session is invalid or expired. Log in again.', 'error');
            } else {
                showStatus(result.message || `Transfer failed (HTTP ${response.status}).`, 'error');
            }
            return;
        }

        if (typeof result.balance === 'number') {
            balanceElement.textContent = formatRupees(result.balance);
        } else if (Number.isInteger(result.balancePaise)) {
            balanceElement.textContent = formatRupees(result.balancePaise / 100);
        }

        showStatus(result.message || 'Transfer completed successfully.', 'success');
        amountInput.value = '';
    } catch (error) {
        showStatus('Could not connect to the ATM backend. Confirm that the server is running.', 'error');
        console.error('Transfer request failed:', error);
    } finally {
        submitButton.disabled = false;
        submitButton.textContent = 'Transfer funds';
    }
});
