/**
 * @file device.js
 * @brief device页面逻辑
 */
import { 
    startPolling, 
    stopPolling 
} from '../js_lib/poll.js';

let pollTimer = null;
const POLL_INTERVAL = 1 * 1000;         // 1s一次

const bh1750_lux = document.getElementById('dev_light');

async function deviceSyncData() {
    try {
        const response = await fetch('/api/device/sync', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const json = await response.json();

        // 更新数据
        if(bh1750_lux)
            bh1750_lux.textContent = `${json.bh1750.lux.toFixed(2)}`;
    } catch (err) {
        console.error("[DEVICE] Sync failed:", err);
    }
}


export const deviceSyncHdl = {
    init: () => {
        pollTimer = startPolling(deviceSyncData, POLL_INTERVAL);
    },
    refresh: deviceSyncData,
    destroy: () => {
        stopPolling(pollTimer);
        pollTimer = null;
    }
};