/**
 * @file poll.js
 * @brief 轮询定时器实现
 */

/**
 * @brief 启动轮询机制
 * @param {Function} fetchFn - 异步拉取数据的函数
 * @param {number} interval - 轮询间隔 (毫秒)
 * @returns {number} 定时器 ID
 */
export function startPolling(fetchFn, interval) {
    // 立即执行一次
    fetchFn();
    const timer = setInterval(fetchFn, interval);
    console.log(`[ChartUtils] Polling started (every ${interval / 1000}s).`);
    return timer;
}

/**
 * @brief 停止轮询机制
 * @param {number} timer - 定时器 ID
 */
export function stopPolling(timer) {
    if (timer) {
        clearInterval(timer);
    }
}