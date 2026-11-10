/**
 * @file sht30.js
 * @brief SHT30 温湿度趋势图模块
 */

import { 
    createLineChart, 
    updateChartData, 
    destroyChart
} from '../js_lib/line_chart.js';

import { 
    startPolling, 
    stopPolling 
} from '../js_lib/poll.js';

let chartInstance = null;
let pollTimer = null;
const POLL_INTERVAL = 15 * 1000; 

/**
 * @brief SHT30 配置
 */
const SHT30_CHART_CONFIG = {
    theme: 'default', // 使用默认主题（红+青）
    datasets: [
        {
            label: 'Temperature (℃)',
            data: [],
            yAxisID: 'yLeft' 
        },
        {
            label: 'Humidity (%RH)',
            data: [],
            yAxisID: 'yRight' 
        }
    ],
    scales: {
        yLeft: { 
            position: 'left',
            title: { display: true, text: '℃' }
        },
        yRight: { 
            position: 'right',
            title: { display: true, text: '%RH' }
        }
    }
};

async function syncData() {
    if (!chartInstance) return;
    try {
        const response = await fetch('/api/sht30/history', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const json = await response.json();

        updateChartData(chartInstance, {
            labels: json.labels || [],
            values: [json.temp || [], json.humi || []]
        });
    } catch (err) {
        console.error("[SHT30] Sync failed:", err);
    }
}

export const SHT30HistoryChart = {
    init: () => {
        chartInstance = createLineChart('SHT30HistoryChart', SHT30_CHART_CONFIG);
        if (chartInstance) {
            pollTimer = startPolling(syncData, POLL_INTERVAL);
        }
    },
    refresh: syncData,
    destroy: () => {
        stopPolling(pollTimer);
        pollTimer = null;
        chartInstance = destroyChart(chartInstance);
    }
};