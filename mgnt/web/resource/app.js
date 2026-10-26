document.addEventListener('DOMContentLoaded', function(){

    // ----- 页面切换逻辑 -----
    const navItems = document.querySelectorAll('.nav-item');
    const pages = {
        dashboard: document.getElementById('page-dashboard'),
        devices: document.getElementById('page-devices'),
        network: document.getElementById('page-network'),
        system: document.getElementById('page-system'),
    };

    function switchPage(pageId) {
        Object.values(pages).forEach(p => p.classList.remove('active-page'));
        if (pages[pageId]) pages[pageId].classList.add('active-page');
        navItems.forEach(item => item.classList.remove('active'));
        document.querySelector(`.nav-item[data-page="${pageId}"]`)?.classList.add('active');
    }

    navItems.forEach(item => {
        item.addEventListener('click', function(e) {
            e.preventDefault();
            const page = this.dataset.page;
            if (page) switchPage(page);
        });
    });

    switchPage('dashboard');

    // ----- 原有功能 -----
    let realtimeTimer = null;
    let syncTimer = null;

    function startPolling() {
        if (!realtimeTimer) realtime_poll();
        if (!syncTimer) sync_poll();
    }

    function stopPolling() {
        clearTimeout(realtimeTimer);
        clearTimeout(syncTimer);
        realtimeTimer = null;
        syncTimer = null;
    }

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            stopPolling();
            console.debug('Page hidden, polling paused');
        } else {
            startPolling();
            console.debug('Page visible, polling resumed');
        }
    });

    /* =-=-=-=-=-=-=-=- LED -=-=-=-=-=-=-=-= */
    const led_turn_btn = document.getElementById('led_turn_btn');
    const led_poll_btn = document.getElementById('led_poll_btn');
    const led_status = document.getElementById('led_status');

    function update_led_status(status) {
        if(status === 'on') {
            led_status.textContent = 'Led Status: ON';
            led_status.style.color = 'green';
        } else if (status === 'off') {
            led_status.textContent = 'Led Status: OFF';
            led_status.style.color = 'red';
        } else {
            led_status.textContent = 'Led Status: UNKNOWN';
            led_status.style.color = 'gray';
        }
    }

    led_turn_btn.addEventListener('click', function(){
        led_turn_btn.disabled = true;
        led_turn_btn.textContent = '请求中...';
        fetch('/api/led/turn', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        })
        .then(response => {
            if(!response.ok) throw new Error('Network response was not ok');
            return response.json();
        })
        .then(data => update_led_status(data.status))
        .catch(error => {
            console.error('Error:', error);
            update_led_status('unknown');
        })
        .finally(() => {
            led_turn_btn.disabled = false;
            led_turn_btn.textContent = 'Turn On/Off';
        });
    });

    led_poll_btn.addEventListener('click', function() {
        led_poll_btn.disabled = true;
        led_poll_btn.textContent = '请求中';
        fetch('/api/led/status', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        })
        .then(response => {
            if(!response.ok) throw new Error('Network response was not ok');
            return response.json();
        })
        .then(data => update_led_status(data.led_status))
        .catch(() => update_led_status('unknown'))
        .finally(() => {
            led_poll_btn.disabled = false;
            led_poll_btn.textContent = 'Poll';
        });
    });

    /* =-=-=-=-=-=-=-=- SHT30 -=-=-=-=-=-=-=-= */
    const env_temp = document.getElementById('env_temp');
    const env_humi = document.getElementById('env_humi');
    const sht30_poll_btn = document.getElementById('sht30_poll_btn');

    sht30_poll_btn.addEventListener('click', function() {
        sht30_poll_btn.disabled = true;
        sht30_poll_btn.textContent = '请求中...';
        fetch('/api/sht30/sensor', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        })
        .then(r => {
            if(!r.ok) throw new Error('Network response was not ok');
            return r.json();
        })
        .then(data => {
            env_temp.textContent = `Temperature: ${data.temperature.toFixed(1)} ℃`;
            env_humi.textContent = `Humidity: ${data.humidity.toFixed(1)} %RH`;
        })
        .catch(() => {
            env_temp.textContent = `Temperature: -- ℃`;
            env_humi.textContent = `Humidity: -- %RH`;
        })
        .finally(() => {
            sht30_poll_btn.disabled = false;
            sht30_poll_btn.textContent = 'Poll';
        });
    });

    /* =-=-=-=-=-=-=-=- realtime -=-=-=-=-=-=-=-= */
    const realtime = document.getElementById('realtime');

    async function realtime_poll() {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000);
        try {
            const r = await fetch('/api/sys/realtime', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                signal: controller.signal
            });
            clearTimeout(timeoutId);
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            const data = await r.json();
            realtime.textContent = data.realtime;
        } catch (err) {
            if (err.name === 'AbortError') {
                console.warn('Realtime poll timed out');
            } else {
                console.warn('Realtime poll failed:', err.message);
            }
            realtime.textContent = '-- : -- : -- -- : -- : --';
        } finally {
            clearTimeout(timeoutId);
            realtimeTimer = setTimeout(realtime_poll, 1000);
        }
    }
    realtime_poll();

    /* =-=-=-=-=-=-=-=- network stat -=-=-=-=-=-=-=-= */
    const eth0_rx = document.getElementById('eth0_rx');
    const wlan0_rx = document.getElementById('wlan0_rx');
    const network_poll_btn = document.getElementById('network_poll_btn');

    network_poll_btn.addEventListener('click', function() {
        network_poll_btn.disabled = true;
        network_poll_btn.textContent = '请求中';
        fetch('/api/network/status/rx', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        })
        .then(r => {
            if(!r.ok) throw new Error('Network response was not ok');
            return r.json();
        })
        .then(data => {
            const eth0_rx_size = (Number(data.eth0.rx_size) / 1000 / 1000).toFixed(3);
            const wlan0_rx_size = (Number(data.wlan0.rx_size) / 1000 / 1000).toFixed(3);
            eth0_rx.textContent = `eth0: 👇 ${data.eth0.rx_rate} kB/s, ${eth0_rx_size} MB`;
            wlan0_rx.textContent = `wlan0: 👇 ${data.wlan0.rx_rate} kB/s, ${wlan0_rx_size} MB`;
        })
        .catch(() => {
            eth0_rx.textContent = 'eth0: 👇 -- kB/s, -- MB';
            wlan0_rx.textContent = 'wlan0: 👇 -- kB/s, -- MB';
        })
        .finally(() => {
            network_poll_btn.disabled = false;
            network_poll_btn.textContent = 'Poll';
        });
    });

    /* =-=-=-=-=-=-=-=- sysmon -=-=-=-=-=-=-=-= */
    const cpu_total = document.getElementById('cpu_total');
    const cpu0 = document.getElementById('cpu0');
    const cpu1 = document.getElementById('cpu1');
    const cpu2 = document.getElementById('cpu2');
    const cpu3 = document.getElementById('cpu3');
    const sysmon_poll_btn = document.getElementById('sysmon_poll_btn');

    function update_cpu_usage(elem, value, threshold=75) {
        elem.textContent = `${value} %`;
        elem.style.color = value > threshold ? 'red' : '';
    }

    sysmon_poll_btn.addEventListener('click', function() {
        sysmon_poll_btn.disabled = true;
        sysmon_poll_btn.textContent = '请求中';
        fetch('/api/sysmon/cpu/usage', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
        })
        .then(r => {
            if(!r.ok) throw new Error('Network response was not ok');
            return r.json();
        })
        .then(data => {
            update_cpu_usage(cpu_total, data.total, 25);
            update_cpu_usage(cpu0, data.cpu0);
            update_cpu_usage(cpu1, data.cpu1);
            update_cpu_usage(cpu2, data.cpu2);
            update_cpu_usage(cpu3, data.cpu3);
        })
        .catch(() => {
            cpu_total.textContent = '-- %';
            cpu0.textContent = '-- %';
            cpu1.textContent = '-- %';
            cpu2.textContent = '-- %';
            cpu3.textContent = '-- %';
        })
        .finally(() => {
            sysmon_poll_btn.disabled = false;
            sysmon_poll_btn.textContent = 'Poll';
        });
    });

    /* =-=-=-=-=-=-=-=- stream 流量大小柱状图 (eth0 & wlan0) -=-=-=-=-=-=-=-= */
    
    // 1. 提取公共的颜色和配置
    const streamChartColors = [
        'rgba(54, 162, 235, 0.8)', 'rgba(255, 99, 132, 0.8)',
        'rgba(75, 192, 192, 0.8)', 'rgba(104, 112, 196, 0.8)',
        'rgba(223, 134, 70, 0.8)', 'rgba(215, 68, 208, 0.8)',
        'rgba(255, 206, 86, 0.8)'
    ];

    // 2. 过滤掉 data 为 0 的项，并同步映射颜色
    function filterStreamData(labels, data) {
        const fLabels = [];
        const fData = [];
        const fBgColors = [];
        const fBorderColors = [];
        let colorIdx = 0;

        for (let i = 0; i < labels.length; i++) {
            if (data[i] > 0) {
                fLabels.push(labels[i]);
                fData.push(data[i]);
                const color = streamChartColors[colorIdx % streamChartColors.length];
                fBgColors.push(color);
                fBorderColors.push(color.replace('0.8', '1'));
                colorIdx++;
            }
        }

        if (fData.length === 0) {
            return {
                labels: ['No Traffic'],
                data: [1], 
                bgColors: ['rgba(200, 200, 200, 0.3)'],
                borderColors: ['rgba(200, 200, 200, 0.5)'],
                isEmpty: true
            };
        }

        return { labels: fLabels, data: fData, bgColors: fBgColors, borderColors: fBorderColors, isEmpty: false };
    }

    // 3. 封装图表创建函数 (由 doughnut 改为 bar)
    function createStreamChart(canvasId) {
        const canvas = document.getElementById(canvasId);
        if (!canvas || typeof Chart === 'undefined') return null;

        return new Chart(canvas.getContext('2d'), {
            type: 'bar',
            data: {
                labels: ['Waiting for backend...'],
                datasets: [{
                    label: 'pkts',
                    data: [1],
                    backgroundColor: streamChartColors,
                    borderColor: streamChartColors.map(c => c.replace('0.8', '1')),
                    borderWidth: 2,
                    borderRadius: 4,
                    hoverBackgroundColor: streamChartColors.map(c => c.replace('0.8', '1'))
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: canvasId.includes('eth0') ? 'eth0 Traffic Size Distribution' : 'wlan0 Traffic Size Distribution',
                        font: { size: 16, weight: 'bold' },
                        padding: { bottom: 15 }
                    },
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: 'rgba(0, 0, 0, 0.85)',
                        padding: 12,
                        cornerRadius: 8,
                        callbacks: {
                            label: function(context) {
                                let label = context.label || '';
                                let value = context.parsed.y || 0;
                                
                                if (label === 'No Traffic' || label === 'Error') return ` ${label}`;
                                if (!value || value <= 0) return ` ${label}: 暂无数据`;
                                
                                let total = context.dataset.data.reduce((a, b) => a + (b || 0), 0);
                                let percentage = total > 0 ? ((value / total) * 100).toFixed(1) : 0;
                                return ` ${label}: ${value.toLocaleString()} pkts (${percentage}%)`; 
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        grid: { display: false },
                        ticks: { font: { size: 12, weight: 'bold' } }
                    },
                    y: {
                        type: 'logarithmic',
                        min: 1,
                        grid: { color: 'rgba(200, 200, 200, 0.2)' },
                        ticks: {
                            font: { size: 11 },
                            color: '#cdd6f4',
                            callback: function(value) {
                                const log10 = Math.log10(value);
                                if (!Number.isInteger(log10)) return null;
                                if (value === 1) return '1';
                                if (value === 10) return '10';
                                if (value === 100) return '100';
                                if (value === 1000) return '1k';
                                if (value === 10000) return '10k';
                                if (value === 100000) return '100k';
                                if (value === 1000000) return '1M';
                                return value.toExponential(0);
                            }
                        }
                    }
                }
            }
        });
    }

    // 4. 分别创建两个图表实例
    let eth0ChartInstance = createStreamChart('eth0_stream_bar'); 
    let wlan0ChartInstance = createStreamChart('wlan0_stream_bar');

    // 5. 供外部调用的正常数据更新函数
    function updateStreamChart(chartInstance, rawLabels, rawData) {
        if (!chartInstance) return;
        const filtered = filterStreamData(rawLabels, rawData);
        chartInstance.data.labels = filtered.labels;
        chartInstance.data.datasets[0].data = filtered.data;
        chartInstance.data.datasets[0].backgroundColor = filtered.bgColors;
        chartInstance.data.datasets[0].borderColor = filtered.borderColors;
        chartInstance.data.datasets[0].hoverBackgroundColor = filtered.borderColors;
        chartInstance.update();
    }

    // 6. 封装错误状态处理函数 (适配柱状图与对数轴)
    function setStreamChartError(chartInstance) {
        if (!chartInstance) return;
        chartInstance.data.labels = ['Error'];
        chartInstance.data.datasets[0].data = [1];
        chartInstance.data.datasets[0].backgroundColor = ['rgba(255, 204, 204, 0.5)'];
        chartInstance.data.datasets[0].borderColor = ['#ff9999'];
        chartInstance.data.datasets[0].hoverBackgroundColor = ['#ff9999'];
        chartInstance.update();
    }

    /* =-=-=-=-=-=-=-=- protocol 柱状图 (eth0 & wlan0) -=-=-=-=-=-=-=-= */

    const barChartColors = [
        'rgba(54, 162, 235, 0.8)', 'rgba(255, 99, 132, 0.8)',
        'rgba(75, 192, 192, 0.8)', 'rgba(104, 112, 196, 0.8)',
        'rgba(223, 134, 70, 0.8)', 'rgba(215, 68, 208, 0.8)',
        'rgba(255, 206, 86, 0.8)'
    ];

    function filterChartData(labels, data) {
        const fLabels = [];
        const fData = [];
        const fBgColors = [];
        const fBorderColors = [];
        let colorIdx = 0;

        for (let i = 0; i < labels.length; i++) {
            if (data[i] > 0) {
                fLabels.push(labels[i]);
                fData.push(data[i]);
                const color = barChartColors[colorIdx % barChartColors.length];
                fBgColors.push(color);
                fBorderColors.push(color.replace('0.8', '1'));
                colorIdx++;
            }
        }

        if (fData.length === 0) {
            return {
                labels: ['No Traffic'],
                data: [1],
                bgColors: ['rgba(200, 200, 200, 0.3)'],
                borderColors: ['rgba(200, 200, 200, 0.5)'],
                isEmpty: true
            };
        }

        return { labels: fLabels, data: fData, bgColors: fBgColors, borderColors: fBorderColors, isEmpty: false };
    }

    function createProtocolBarChart(canvasId) {
        const canvas = document.getElementById(canvasId);
        if (!canvas || typeof Chart === 'undefined') return null;

        return new Chart(canvas.getContext('2d'), {
            type: 'bar',
            data: {
                labels: ['Waiting for backend'],
                datasets: [{
                    label: 'pkts',
                    data: [1],
                    backgroundColor: barChartColors,
                    borderColor: barChartColors.map(c => c.replace('0.8', '1')),
                    borderWidth: 2,
                    borderRadius: 4,
                    hoverBackgroundColor: barChartColors.map(c => c.replace('0.8', '1'))
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: canvasId.includes('eth0') ? 'eth0 Protocol Distribution' : 'wlan0 Protocol Distribution',
                        font: { size: 16, weight: 'bold' },
                        padding: { bottom: 15 }
                    },
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: 'rgba(0, 0, 0, 0.85)',
                        padding: 12,
                        cornerRadius: 8,
                        callbacks: {
                            label: function(context) {
                                let protocol = context.label || '';
                                let value = context.parsed.y || 0;
                                if (protocol === 'No Traffic' || protocol === 'Error') return ` ${protocol}`;
                                if (!value || value <= 0) return ` ${protocol}: 暂无数据`;
                                let total = context.dataset.data.reduce((a, b) => a + (b || 0), 0);
                                let percentage = total > 0 ? ((value / total) * 100).toFixed(1) : 0;
                                return ` ${protocol}: ${value.toLocaleString()} pkts (${percentage}%)`;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        grid: { display: false },
                        ticks: { font: { size: 12, weight: 'bold' } }
                    },
                    y: {
                        type: 'logarithmic',
                        min: 1,
                        grid: { color: 'rgba(200, 200, 200, 0.2)' },
                        ticks: {
                            font: { size: 11 },
                            color: '#cdd6f4',
                            callback: function(value) {
                                const log10 = Math.log10(value);
                                if (!Number.isInteger(log10)) return null;
                                if (value === 1) return '1';
                                if (value === 10) return '10';
                                if (value === 100) return '100';
                                if (value === 1000) return '1k';
                                if (value === 10000) return '10k';
                                if (value === 100000) return '100k';
                                if (value === 1000000) return '1000k';
                                return value.toExponential(0);
                            }
                        }
                    }
                }
            }
        });
    }

    let eth0BarInstance = createProtocolBarChart('eth0_protocol_bar');
    let wlan0BarInstance = createProtocolBarChart('wlan0_protocol_bar');

    function updateBarChart(chartInstance, rawLabels, rawData) {
        if (!chartInstance) return;
        const filtered = filterChartData(rawLabels, rawData);
        chartInstance.data.labels = filtered.labels;
        chartInstance.data.datasets[0].data = filtered.data;
        chartInstance.data.datasets[0].backgroundColor = filtered.bgColors;
        chartInstance.data.datasets[0].borderColor = filtered.borderColors;
        chartInstance.data.datasets[0].hoverBackgroundColor = filtered.borderColors;
        chartInstance.update();
    }

    function setBarChartError(chartInstance) {
        if (!chartInstance) return;
        chartInstance.data.labels = ['Error'];
        chartInstance.data.datasets[0].data = [1];
        chartInstance.data.datasets[0].backgroundColor = ['rgba(255, 204, 204, 0.5)'];
        chartInstance.data.datasets[0].borderColor = ['#ff9999'];
        chartInstance.data.datasets[0].hoverBackgroundColor = ['#ff9999'];
        chartInstance.update();
    }

    /* =-=-=-=-=-=-=-=- sync 定时刷新 -=-=-=-=-=-=-=-= */
    async function sync_poll() {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000);
        try{
            const r = await fetch('/api/sync', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                signal: controller.signal,
            });
            clearTimeout(timeoutId);
            if(!r.ok) throw new Error('Network response was not ok');
            const data = await r.json();

            update_led_status(data.led.led_status);
            env_temp.textContent = `Temperature: ${data.sht30.temperature.toFixed(1)} ℃`;
            env_humi.textContent = `Humidity: ${data.sht30.humidity.toFixed(1)} %RH`;
            update_cpu_usage(cpu_total, data.sysmon.total, 25);
            update_cpu_usage(cpu0, data.sysmon.cpu0);
            update_cpu_usage(cpu1, data.sysmon.cpu1);
            update_cpu_usage(cpu2, data.sysmon.cpu2);
            update_cpu_usage(cpu3, data.sysmon.cpu3);

            const eth0_rx_size = (Number(data.network.eth0.rx_size) / 1024 / 1024).toFixed(3);
            const wlan0_rx_size = (Number(data.network.wlan0.rx_size) / 1024 / 1024).toFixed(3);
            eth0_rx.textContent = `eth0: 👇 ${data.network.eth0.rx_rate} kB/s, ${eth0_rx_size} MB`;
            wlan0_rx.textContent = `wlan0: 👇 ${data.network.wlan0.rx_rate} kB/s, ${wlan0_rx_size} MB`;

            // --- 获取卡片 DOM 元素 ---
            const cardEth0 = document.getElementById('card-eth0');
            const cardWlan0 = document.getElementById('card-wlan0');

            // --- 辅助判断函数：检查后端是否返回了该网卡的有效数据 ---
            function hasValidData(netInterface) {
                const hasProtocol = data.protocol_bar && Array.isArray(data.protocol_bar[netInterface]) && data.protocol_bar[netInterface].length > 0;
                const hasStream = data.stream_pie && Array.isArray(data.stream_pie[netInterface]) && data.stream_pie[netInterface].length > 0;
                return hasProtocol || hasStream;
            }

            // ================= 处理 eth0 =================
            if (hasValidData('eth0')) {
                // 有数据：显示卡片
                if (cardEth0) cardEth0.style.display = ''; 
                
                // 更新流量柱状图
                if (data.stream_pie && Array.isArray(data.stream_pie.labels) && Array.isArray(data.stream_pie.eth0)) {
                    updateStreamChart(eth0ChartInstance, data.stream_pie.labels, data.stream_pie.eth0);
                }
                // 更新协议柱状图
                if (data.protocol_bar && Array.isArray(data.protocol_bar.labels) && Array.isArray(data.protocol_bar.eth0)) {
                    updateBarChart(eth0BarInstance, data.protocol_bar.labels, data.protocol_bar.eth0);
                }
            } else {
                // 无数据：彻底隐藏整个卡片
                if (cardEth0) cardEth0.style.display = 'none';
            }

            // ================= 处理 wlan0 =================
            if (hasValidData('wlan0')) {
                // 有数据：显示卡片
                if (cardWlan0) cardWlan0.style.display = '';
                
                // 更新流量柱状图
                if (data.stream_pie && Array.isArray(data.stream_pie.labels) && Array.isArray(data.stream_pie.wlan0)) {
                    updateStreamChart(wlan0ChartInstance, data.stream_pie.labels, data.stream_pie.wlan0);
                }
                // 更新协议柱状图
                if (data.protocol_bar && Array.isArray(data.protocol_bar.labels) && Array.isArray(data.protocol_bar.wlan0)) {
                    updateBarChart(wlan0BarInstance, data.protocol_bar.labels, data.protocol_bar.wlan0);
                }
            } else {
                // 无数据：彻底隐藏整个卡片
                if (cardWlan0) cardWlan0.style.display = 'none';
            }
        } catch (err) {
            if (err.name === 'AbortError') {
                console.warn('sync timeout');
            } else {
                console.warn('sync fail: ', err.message);
            }
            update_led_status('unknown');
            env_temp.textContent = `Temperature: -- ℃`;
            env_humi.textContent = `Humidity: -- %RH`;
            cpu_total.textContent = '-- %';
            cpu0.textContent = '-- %';
            cpu1.textContent = '-- %';
            cpu2.textContent = '-- %';
            cpu3.textContent = '-- %';
            eth0_rx.textContent = 'eth0: 👇 -- kB/s, -- MB';
            wlan0_rx.textContent = 'wlan0: 👇 -- kB/s, -- MB';

            setStreamChartError(eth0ChartInstance);
            setStreamChartError(wlan0ChartInstance);

            setBarChartError(eth0BarInstance);
            setBarChartError(wlan0BarInstance);
            // 异常时显示
            const cardEth0 = document.getElementById('card-eth0');
            const cardWlan0 = document.getElementById('card-wlan0');
            if (cardEth0) cardEth0.style.display = '';
            if (cardWlan0) cardWlan0.style.display = '';
        } finally {
            clearTimeout(timeoutId);
            syncTimer = setTimeout(sync_poll, 5000);
        }
    }
    sync_poll();
});