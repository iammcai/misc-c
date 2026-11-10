/**
 * @file chartUtils.js
 * @brief 通用双轴折线图模块 (包含内置颜色主题)
 */

// ==========================================
// ⭐ 1. 定义全局颜色主题 (调色板)
// ==========================================
export const CHART_THEMES = {
    // 默认主题：适合温湿度、常规数据
    default: [
        { border: '#ff6b6b', bg: 'rgba(255, 107, 107, 0.1)', text: '#ff6b6b' }, // 红/暖色 (常用于温度)
        { border: '#4ecdc4', bg: 'rgba(78, 205, 196, 0.1)',   text: '#4ecdc4' }, // 青/冷色 (常用于湿度)
        { border: '#ffe66d', bg: 'rgba(255, 230, 109, 0.1)',  text: '#d4b832' }, // 黄
        { border: '#1a535c', bg: 'rgba(26, 83, 92, 0.1)',     text: '#1a535c' }, // 深蓝
    ],
    // 科技蓝主题：适合大屏展示
    tech: [
        { border: '#00f2fe', bg: 'rgba(0, 242, 254, 0.1)',   text: '#00f2fe' },
        { border: '#4facfe', bg: 'rgba(79, 172, 254, 0.1)',  text: '#4facfe' },
        { border: '#43e97b', bg: 'rgba(67, 233, 123, 0.1)',  text: '#43e97b' },
    ]
};

const DEFAULT_OPTIONS = {
    responsive: true,
    maintainAspectRatio: false,
    animation: { duration: 300 },
    interaction: { mode: 'index', intersect: false },
    plugins: {
        legend: { 
            position: 'top', 
            labels: { usePointStyle: true, pointStyle: 'circle', boxWidth: 6, boxHeight: 6 } 
        }
    },
    scales: {
        x: { grid: { display: false } },
        yLeft: { 
            type: 'linear', position: 'left', grace: '10%',
            grid: { color: 'rgba(0,0,0,0.04)', borderDash: [3, 3] } 
        },
        yRight: { 
            type: 'linear', position: 'right', grace: '10%',
            grid: { drawOnChartArea: false } 
        }
    }
};

/**
 * @brief 创建并初始化图表实例
 * @param {string|HTMLCanvasElement} canvasIdOrEl
 * 
 * @param {Object} config 
 * @param {Array} config.datasets - 业务层只需传 label 和 yAxisID，无需传颜色
 * @param {string} [config.theme='default'] - 指定使用的主题名称
 */
export function createLineChart(canvasIdOrEl, config) {
    const ctx = typeof canvasIdOrEl === 'string' 
        ? document.getElementById(canvasIdOrEl) 
        : canvasIdOrEl;

    if (!ctx) return null;

    // 2. 获取主题，如果没有指定则用 default
    const themeName = config.theme || 'default';
    const palette = CHART_THEMES[themeName] || CHART_THEMES.default;

    // 3. 自动为 datasets 注入颜色和样式
    const styledDatasets = (config.datasets || []).map((ds, index) => {
        // 循环取色，防止数据集数量超过调色板数量
        const colors = palette[index % palette.length]; 
        
        return {
            ...ds,
            borderColor: ds.borderColor || colors.border,       // 允许业务层覆盖，否则用主题色
            backgroundColor: ds.backgroundColor || colors.bg,
            borderWidth: ds.borderWidth ?? 2,
            pointRadius: ds.pointRadius ?? 2,
            tension: ds.tension ?? 0.3,
            fill: ds.fill ?? true,
        };
    });

    // 4. 自动为坐标轴 (scales) 注入对应的文字颜色
    const userScales = config.scales || {};
    const mergedScales = { ...DEFAULT_OPTIONS.scales };

    for (const key in userScales) {
        const axisConfig = { ...userScales[key] };
        
        // 如果坐标轴没有手动指定颜色，且它是 yLeft/yRight，尝试匹配对应 dataset 的颜色
        if (key === 'yLeft' && styledDatasets[0]) {
            const c = palette[0].text;
            axisConfig.title = { ...(axisConfig.title || {}), color: axisConfig.title?.color || c };
            axisConfig.ticks = { ...(axisConfig.ticks || {}), color: axisConfig.ticks?.color || c };
        }
        if (key === 'yRight' && styledDatasets[1]) {
            const c = palette[1 % palette.length].text;
            axisConfig.title = { ...(axisConfig.title || {}), color: axisConfig.title?.color || c };
            axisConfig.ticks = { ...(axisConfig.ticks || {}), color: axisConfig.ticks?.color || c };
        }

        mergedScales[key] = { ...mergedScales[key], ...axisConfig };
    }

    const finalOptions = {
        ...DEFAULT_OPTIONS,
        ...(config.options || {}),
        scales: mergedScales
    };

    return new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: styledDatasets
        },
        options: finalOptions
    });
}

/**
 * @brief 全量覆盖更新图表数据
 * @param {Chart} chartInstance - Chart.js 实例
 * @param {Object} data - 新数据
 * @param {Array<string>} data.labels - X轴标签数组
 * @param {Array<Array<number>>} data.values - 对应 datasets 的数据二维数组，如 [[temp...], [humi...]]
 */
export function updateChartData(chartInstance, data) {
    if (!chartInstance) return;

    chartInstance.data.labels = data.labels || [];
    
    if (data.values && Array.isArray(data.values)) {
        data.values.forEach((valArray, index) => {
            if (chartInstance.data.datasets[index]) {
                chartInstance.data.datasets[index].data = valArray;
            }
        });
    }

    chartInstance.update();
}

/**
 * @brief 销毁图表实例
 * @param {Chart} chartInstance - Chart.js 实例
 * @returns {null}
 */
export function destroyChart(chartInstance) {
    if (chartInstance) {
        chartInstance.destroy();
    }
    return null;
}