/**
 * @file sht30.js
 * @brief SHT30 温湿度趋势图模块 (全量覆盖模式)
 */

let chartInstance = null;
let pollTimer = null;

// ⭐ 配置项：你的 C 服务器返回数据的 API 路径
const API_URL = '/api/sht30/history'; 
// ⭐ 配置项：轮询间隔 (60秒)
const POLL_INTERVAL = 60 * 1000; 

/**
 * @brief 初始化空图表
 */
function initChart() {
    const ctx = document.getElementById('sht30Chart');
    if (!ctx) return false;

    chartInstance = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [], // 初始为空，等待数据填充
            datasets: [
                {
                    label: 'Temperature (℃)',
                    data: [],
                    borderColor: '#ff6b6b',
                    backgroundColor: 'rgba(255, 107, 107, 0.1)',
                    borderWidth: 2, pointRadius: 2, tension: 0.3, fill: true, yAxisID: 'yTemp'
                },
                {
                    label: 'Humidity (%RH)',
                    data: [],
                    borderColor: '#4ecdc4',
                    backgroundColor: 'rgba(78, 205, 196, 0.1)',
                    borderWidth: 2, pointRadius: 2, tension: 0.3, fill: true, yAxisID: 'yHumi'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: { duration: 300 }, // 缩短动画，让 60s 刷新时不突兀
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { position: 'top', labels: { usePointStyle: true, pointStyle: 'circle', boxWidth: 6, boxHeight: 6 } }
            },
            scales: {
                x: { grid: { display: false } },
                yTemp: { 
                    type: 'linear', position: 'left', grace: '10%',
                    title: { display: true, text: '℃', color: '#ff6b6b' }, 
                    ticks: { color: '#ff6b6b' }, grid: { color: 'rgba(0,0,0,0.04)' } 
                },
                yHumi: { 
                    type: 'linear', position: 'right', grace: '10%',
                    title: { display: true, text: '%RH', color: '#4ecdc4' }, 
                    ticks: { color: '#4ecdc4' }, grid: { drawOnChartArea: false } 
                }
            }
        }
    });
    return true;
}

/**
 * @brief 拉取数据并【全量覆盖】图表
 */
async function syncData() {
    if (!chartInstance) return;

    try {
        // ⭐ 1. URL 保持干净，没有 ? 后缀
        const response = await fetch('/api/sht30/history', {
            method: 'POST',             // ⭐ 2. 指定为 POST 方法
            headers: {
                'Content-Type': 'application/json' // 告诉 C 服务器发的是 JSON
            },
            body: JSON.stringify({      // ⭐ 3. POST 的请求体（可以传空对象或指令）
                action: "get_history"   
            })
        });

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        
        const json = await response.json();

        // 全量覆盖图表数据
        chartInstance.data.labels = json.labels || [];
        chartInstance.data.datasets[0].data = json.temp || [];
        chartInstance.data.datasets[1].data = json.humi || [];

        chartInstance.update();
        
    } catch (err) {
        console.error("[SHT30] Sync failed:", err);
    }
}

/**
 * @brief 启动 60s 轮询
 */
function startPolling() {
    if (pollTimer) clearInterval(pollTimer);
    
    // 页面加载时立即执行一次
    syncData();
    
    // 每 60 秒执行一次
    pollTimer = setInterval(syncData, POLL_INTERVAL);
    console.log(`[SHT30] Polling started (every ${POLL_INTERVAL/1000}s).`);
}

/**
 * @brief 停止轮询
 */
function stopPolling() {
    if (pollTimer) {
        clearInterval(pollTimer);
        pollTimer = null;
    }
}

// ==========================================
// 对外暴露的公共 API
// ==========================================
export const SHT30Chart = {
    init: () => {
        if (initChart()) {
            startPolling();
        }
    },
    refresh: syncData, // 供外部按钮手动刷新调用
    destroy: () => {
        stopPolling();
        if (chartInstance) {
            chartInstance.destroy();
            chartInstance = null;
        }
    }
};
