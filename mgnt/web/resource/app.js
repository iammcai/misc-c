document.addEventListener('DOMContentLoaded', function(){

    /* =-=-=-=-=-=-=-=- LED -=-=-=-=-=-=-=-= */

    // 定义变量指向按钮和状态展示，对应html文件的id
    const led_turn_btn = document.getElementById('led_turn_btn');
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

    // 给led按钮添加一个点击事件
    led_turn_btn.addEventListener('click', function(){
        // 禁用按钮点击
        led_turn_btn.disabled = true;
        led_turn_btn.textContent = '请求中...';

        fetch('/api/led/turn', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
        })
        .then(response => {
            if(!response.ok){
                throw new Error('Network response was not ok');
            }
            return response.json();     // 后端返回json
        })
        .then(data => update_led_status(data.status))
        .catch(error => {
            console.error('Error:', error);
            update_led_status('unknown');
        })
        .finally(() => {
            // 恢复按钮文字
            led_turn_btn.disabled = false;
            led_turn_btn.textContent = 'Turn LED';
        })
    })

    // led状态轮询
    function led_status_poll() {
        fetch('/api/led/status', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
        })
        .then(response => {
            if(!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json();
        })
        .then(data => update_led_status(data.status))
        .catch(() => update_led_status('unknown'))
        .finally(() => {
            setTimeout(led_status_poll, 5000);
        })
    }
    // 启动
    led_status_poll();

    /* =-=-=-=-=-=-=-=- SHT30 -=-=-=-=-=-=-=-= */

    const env_temp = document.getElementById('env_temp');
    const env_humi = document.getElementById('env_humi');

    function sht30_poll() {
        fetch('/api/sht30/sensor', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
        })
        .then(r => {
            if(!r.ok) {
                throw new Error('Network response was not ok');
            }
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
            setTimeout(sht30_poll, 5000);
        })
    }
    // 启动轮询
    sht30_poll();

    /* =-=-=-=-=-=-=-=- realtime -=-=-=-=-=-=-=-= */

    const realtime = document.getElementById('realtime');

    function realtime_poll() {
        fetch('/api/sys/realtime', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
        })
        .then(r => {
            if(!r.ok) {
                throw new Error('Network response was not ok');
            }
            return r.json();
        })
        .then(data => {
            realtime.textContent = data.realtime;
        })
        .catch(() => {
            realtime.textContent = '-- : -- : -- -- : -- : --'
        })
        .finally(() => {
            setTimeout(realtime_poll, 1000);
        })
    }
    realtime_poll();

    /* =-=-=-=-=-=-=-=- sysmon -=-=-=-=-=-=-=-= */

    const cpu_total = document.getElementById('cpu_total');
    const cpu0 = document.getElementById('cpu0');
    const cpu1 = document.getElementById('cpu1');
    const cpu2 = document.getElementById('cpu2');
    const cpu3 = document.getElementById('cpu3');

    // 辅助函数，更新cpu利用率展示标签
    function update_cpu_usage(elem, value, threshold=75) {
        elem.textContent = `${value} %`;
        elem.style.color = value > threshold ? 'red' : '';
    }

    function sysmon_poll() {
        fetch('/api/sysmon/cpu/usage', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
        })
        .then(r => {
            if(!r.ok){
                throw new Error('Network response was not ok');
            }
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
            setTimeout(sysmon_poll, 2000);
        })
    }
    sysmon_poll();
})