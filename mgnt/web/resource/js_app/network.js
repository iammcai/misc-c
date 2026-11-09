/**
 * @file network.js
 * @brief network页面逻辑
 */

import { 
    createLineChart, 
    updateChartData, 
    destroyChart, 
    startPolling, 
    stopPolling 
} from '../js_lib/line_chart.js';

let eth0RateChartInstance = null;
let wlan0RateChartInstance = null;
let pollTimer = null;
const POLL_INTERVAL = 1 * 1000;         // 1s一次

/**
 * @brief   RATE折线图配置
 */
const RATE_CHART_CONFIG = {
    theme: 'default',
    options: {
        animation: false
    },
    datasets: [
        {
            label: 'RX Rate',
            data: [],
            yAxisID: 'yLeft' 
        },
        {
            label: 'TX Rate',
            data: [],
            yAxisID: 'yRight' 
        }
    ],
    scales: {
        yLeft: { 
            type: 'logarithmic',
            position: 'left',
            title: { display: true, text: 'Bytes/s (Log)' },
            ticks: {
                callback: function(value) {
                    if (value >= 1024 * 1024) return (value / (1024 * 1024)).toFixed(1) + ' MB/s';
                    if (value >= 1024) return (value / 1024).toFixed(1) + ' KB/s';
                    return value + ' B/s';
                }
            }
        },
        yRight: { 
            type: 'logarithmic',
            position: 'right',
            title: { display: true, text: 'Bytes/s (Log)' },
            ticks: {
                callback: function(value) {
                    if (value >= 1024 * 1024) return (value / (1024 * 1024)).toFixed(1) + ' MB/s';
                    if (value >= 1024) return (value / 1024).toFixed(1) + ' KB/s';
                    return value + ' B/s';
                }
            }
        }
    }
};

async function networkSyncData() {
    try {
        const response = await fetch('/api/network/sync', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const json = await response.json();

        // 更新数据

        // eth0 rx/tx速率折线图
        if(eth0RateChartInstance)
        {
            const rx = json.eth0?.rx?.history || [];
            const tx = json.eth0?.tx?.history || [];
            const safeRx = rx.map(v => v > 0 ? v : 1);
            const safeTx = tx.map(v => v > 0 ? v : 1);
            // 对数坐标轴，去掉0和负数
            const len = Math.max(safeRx.length, safeTx.length);
            const labels = Array.from({ length: len }, (_, i) => (i === len - 1 ? 'now' : `-${len - 1 - i}s`));
            updateChartData(eth0RateChartInstance, {
                labels,
                values: [safeRx, safeTx]
            });
        }
        // wlan0 rx/tx速率折线图
        if(wlan0RateChartInstance)
        {
            const rx = json.wlan0?.rx?.history || [];
            const tx = json.wlan0?.tx?.history || [];
            const safeRx = rx.map(v => v > 0 ? v : 1);
            const safeTx = tx.map(v => v > 0 ? v : 1);
            // 对数坐标轴，去掉0和负数
            const len = Math.max(safeRx.length, safeTx.length);
            const labels = Array.from({ length: len }, (_, i) => (i === len - 1 ? 'now' : `-${len - 1 - i}s`));
            updateChartData(wlan0RateChartInstance, {
                labels,
                values: [safeRx, safeTx]
            });
        }
    } catch (err) {
        console.error("[NETWORK] Sync failed:", err);
    }
}


export const networkSyncHdl = {
    init: () => {
        eth0RateChartInstance = createLineChart('eth0-rate-line-chart', RATE_CHART_CONFIG);
        wlan0RateChartInstance = createLineChart('wlan0-rate-line-chart', RATE_CHART_CONFIG);
        pollTimer = startPolling(networkSyncData, POLL_INTERVAL);
    },
    refresh: networkSyncData,
    destroy: () => {
        stopPolling(pollTimer);
        pollTimer = null;
        eth0RateChartInstance = destroyChart(eth0RateChartInstance);
        wlan0RateChartInstance = destroyChart(wlan0RateChartInstance);
    }
};