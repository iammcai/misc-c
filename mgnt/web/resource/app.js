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
        } finally {
            clearTimeout(timeoutId);
            syncTimer = setTimeout(sync_poll, 5000);
        }
    }
    sync_poll();
});