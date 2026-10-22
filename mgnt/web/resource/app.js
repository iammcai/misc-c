document.addEventListener('DOMContentLoaded', function(){

    // ----- 页面切换逻辑 -----
    const navItems = document.querySelectorAll('.nav-item');
    const pages = {
        dashboard: document.getElementById('page-dashboard'),
        devices: document.getElementById('page-devices'),
        network: document.getElementById('page-network'),
        system: document.getElementById('page-system'),
    };

    // 切换页面函数
    function switchPage(pageId) {
        // 隐藏所有页面
        Object.values(pages).forEach(p => p.classList.remove('active-page'));
        // 显示目标页面
        if (pages[pageId]) pages[pageId].classList.add('active-page');
        // 更新菜单高亮
        navItems.forEach(item => item.classList.remove('active'));
        document.querySelector(`.nav-item[data-page="${pageId}"]`)?.classList.add('active');
    }

    // 监听菜单点击
    navItems.forEach(item => {
        item.addEventListener('click', function(e) {
            e.preventDefault();
            const page = this.dataset.page;
            if (page) switchPage(page);
        });
    });

    // 默认激活 Dashboard
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

    /* =-=-=-=-=-=-=-=- stream 饼图 (eth0 & wlan0) -=-=-=-=-=-=-=-= */
    
    // 1. 提取公共的颜色和配置
    const chartColors = [
        'rgba(54, 162, 235, 0.8)', 'rgba(255, 99, 132, 0.8)',
        'rgba(75, 192, 192, 0.8)', 'rgba(104, 112, 196, 0.8)',
        'rgba(223, 134, 70, 0.8)', 'rgba(215, 68, 208, 0.8)',
        'rgba(255, 206, 86, 0.8)'
    ];
    // 2. 封装图表创建函数
    function createStreamChart(canvasId) {
        const canvas = document.getElementById(canvasId);
        if (!canvas || typeof Chart === 'undefined') return null;

        return new Chart(canvas.getContext('2d'), {
            type: 'doughnut',
            data: {
                labels: ['Waiting for backend...'],
                datasets: [{
                    data: [1],
                    backgroundColor: ['#cccccc'],
                    borderColor: '#ffffff',
                    borderWidth: 3,
                    hoverOffset: 15
                }]
            },
            options: {
                responsive: true,
                cutout: '60%',
                plugins: {
                    title: {            // 标题
                        display: true,
                        text: canvasId.includes('eth0') ? 'eth0 Traffic Size Distribution' : 'wlan0 Traffic Size Distribution',
                        font: { size: 16, weight: 'bold' },
                        padding: { bottom: 15 }
                    },
                    legend: {
                        position: 'bottom',
                        labels: { padding: 15, usePointStyle: true, pointStyle: 'circle', font: { size: 12 } }
                    },
                    tooltip: {
                        backgroundColor: 'rgba(0, 0, 0, 0.8)',
                        padding: 12,
                        cornerRadius: 8,
                        callbacks: {
                            label: function(context) {
                                let label = context.label || '';
                                let value = context.parsed || 0;
                                let total = context.dataset.data.reduce((a, b) => a + b, 0);
                                let percentage = total > 0 ? Math.round((value / total) * 100) : 0;
                                return ` ${label}: ${value} pkts (${percentage}%)`; 
                            }
                        }
                    }
                }
            }
        });
    }
    // 3. 分别创建两个图表实例
    let eth0ChartInstance = createStreamChart('eth0_stream_pie');
    let wlan0ChartInstance = createStreamChart('wlan0_stream_pie');
    // 4. 封装错误状态处理函数
    function setChartError(chartInstance) {
        if (!chartInstance) return;
        chartInstance.data.labels = ['无数据'];
        chartInstance.data.datasets[0].data = [1];
        chartInstance.data.datasets[0].backgroundColor = ['#ffcccc'];
        chartInstance.update();
    }

    /* =-=-=-=-=-=-=-=- protocol 柱状图 (eth0 & wlan0) -=-=-=-=-=-=-=-= */
    // 1. 公共的颜色和配置
    const barChartColors = [
        'rgba(54, 162, 235, 0.8)', 'rgba(255, 99, 132, 0.8)',
        'rgba(75, 192, 192, 0.8)', 'rgba(104, 112, 196, 0.8)',
        'rgba(223, 134, 70, 0.8)', 'rgba(215, 68, 208, 0.8)',
        'rgba(255, 206, 86, 0.8)'
    ];
    // 2. 封装图表创建函数
        function createProtocolBarChart(canvasId) {
        const canvas = document.getElementById(canvasId);
        if (!canvas || typeof Chart === 'undefined') return null;

        return new Chart(canvas.getContext('2d'), {
            type: 'bar',
            data: {
                labels: ['Waiting for backend'],
                datasets: [{
                    label: 'pkts',
                    data: [0],
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
                                if (!value) return ` ${protocol}: 暂无数据`;
                                let total = context.dataset.data.reduce((a, b) => a + (b || 0), 0);
                                let percentage = total > 0 ? ((value / total) * 100).toFixed(1) : 0;
                                return ` ${protocol}: ${value.toLocaleString()} 个 (${percentage}%)`;
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

    // 3. 分别创建两个图表实例
    let eth0BarInstance = createProtocolBarChart('eth0_protocol_bar');
    let wlan0BarInstance = createProtocolBarChart('wlan0_protocol_bar');
    // 4. 封装错误/无数据状态处理函数
    function setBarChartError(chartInstance) {
        if (!chartInstance) return;
        chartInstance.data.labels = ['无数据'];
        chartInstance.data.datasets[0].data = [0];
        chartInstance.data.datasets[0].backgroundColor = ['#ffcccc'];
        chartInstance.data.datasets[0].borderColor = ['#ff9999'];
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

            if (data.stream_pie) {
                // 更新 eth0
                if (eth0ChartInstance && data.stream_pie.eth0) {
                    eth0ChartInstance.data.labels = data.stream_pie.labels;
                    eth0ChartInstance.data.datasets[0].data = data.stream_pie.eth0;
                    eth0ChartInstance.data.datasets[0].backgroundColor = chartColors;
                    eth0ChartInstance.update();
                }
                // 更新 wlan0
                if (wlan0ChartInstance && data.stream_pie.wlan0) {
                    wlan0ChartInstance.data.labels = data.stream_pie.labels;
                    wlan0ChartInstance.data.datasets[0].data = data.stream_pie.wlan0;
                    wlan0ChartInstance.data.datasets[0].backgroundColor = chartColors;
                    wlan0ChartInstance.update();
                }
            }

            if (data.protocol_bar) {
                const labels = data.protocol_bar.labels || [];
                const dynamicBarColors = labels.map((_, i) =>
                    barChartColors[i % barChartColors.length]
                );
                const dynamicBarBorders = dynamicBarColors.map(c => c.replace('0.8', '1'));

                // eth0：先判空再处理
                if (eth0BarInstance && Array.isArray(data.protocol_bar.eth0)) {
                    const safeData = data.protocol_bar.eth0.map(v => v === 0 ? null : v);
                    eth0BarInstance.data.labels = labels;
                    eth0BarInstance.data.datasets[0].data = safeData;
                    eth0BarInstance.data.datasets[0].backgroundColor = dynamicBarColors;
                    eth0BarInstance.data.datasets[0].borderColor = dynamicBarBorders;
                    eth0BarInstance.update();
                }

                // wlan0：同样保护
                if (wlan0BarInstance && Array.isArray(data.protocol_bar.wlan0)) {
                    const safeData = data.protocol_bar.wlan0.map(v => v === 0 ? null : v);
                    wlan0BarInstance.data.labels = labels;
                    wlan0BarInstance.data.datasets[0].data = safeData;
                    wlan0BarInstance.data.datasets[0].backgroundColor = dynamicBarColors;
                    wlan0BarInstance.data.datasets[0].borderColor = dynamicBarBorders;
                    wlan0BarInstance.update();
                }
            }

        } catch (err) {
            if (err.name === 'AbortError') {
                console.warn('sync timeout');
            } else {
                console.warn('sunc fail: ', err.message);
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

            setChartError(eth0ChartInstance);
            setChartError(wlan0ChartInstance);

            setBarChartError(eth0BarInstance);
            setBarChartError(wlan0BarInstance);

        } finally {
            clearTimeout(timeoutId);
            syncTimer = setTimeout(sync_poll, 5000);
        }
    }
    sync_poll();
});