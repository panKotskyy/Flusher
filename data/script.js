var timeout = null;
var slider = document.getElementById("servoSlider");
var servoP = document.getElementById("servoPos");
servoP.innerHTML = slider.value;
slider.oninput = function () {
    slider.value = this.value;
    servoP.innerHTML = this.value;
}

document.addEventListener("DOMContentLoaded", function() {
    // Fetch initial values from the backend
    fetchInitialValues();
    // Initialize time range to last 5 hours, ignoring minutes
    initializeTimeRange();
});

let myChart = null; // Store chart instance globally

// Function to fetch initial values from the backend
async function fetchInitialValues() {
    const response = await fetch('/settings', {
        method: 'GET',
        headers: {
            'Content-Type': 'application/json'
        }
    });

    const settings = await response.json();

    // Update UI elements with initial values
    document.getElementById("deepSleepMode").checked = settings.deepSleepMode;
    document.getElementById("sendData").checked = settings.sendData;
    document.getElementById("debug").checked = settings.debug;
    document.getElementById("enableManual").checked = settings.enableManual;
    document.getElementById("enableAuto").checked = settings.enableAuto;

    document.getElementById("deepSleepInterval").value = settings.deepSleepInterval;
    document.getElementById("sensorReadingInterval").value = settings.sensorReadingInterval;
    document.getElementById("forceSensorSendInterval").value = settings.forceSensorSendInterval;
    document.getElementById("autoFlushDelay").value = settings.autoFlushDelay;

    document.getElementById("autoFlushStartTime").value = minutesToTime(settings.autoFlushStartTime);
    document.getElementById("autoFlushStopTime").value = minutesToTime(settings.autoFlushStopTime);
}

// Function to update a setting
function updateSetting(settingName, value) {
    clearTimeout(timeout);
    timeout = setTimeout(function () {
        fetch(`/set?${settingName}=${value}`, {
            method: 'GET',
            headers: {
                'Content-Type': 'application/json'
            }
        }).then(response => {
            console.log(`${settingName} updated to ${value}`);
        }).catch(error => {
            console.error(`Error updating ${settingName}:`, error);
        });
    }, 500);
}

function timeToMinutes(time) {
    let [hours, mins] = time.split(":");
    return parseInt(hours) * 60 + parseInt(mins);
}

function minutesToTime(minutes) {
    const paddedHours = String(Math.floor(minutes / 60)).padStart(2, '0');
    const paddedMinutes = String(minutes % 60).padStart(2, '0');
    return `${paddedHours}:${paddedMinutes}`;
}

// Function to fetch data from InfluxDB
async function fetchData(startTime, endTime) {
    const response = await fetch('https://us-east-1-1.aws.cloud2.influxdata.com/api/v2/query', {
        method: 'POST',
        headers: {
            'Authorization': 'Token OrddY9ea5LGv5bpZNEdnYrBBn7-4nwL1zrZI_5W-XVYghPNeIbh6a7J-1uNnfzaQsk7E78JHdX7l6ypsZNrXBg==',
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            "query": `from(bucket:\"Flusher\") |> range(start: ${startTime}, stop: ${endTime}) |> filter(fn: (r) => r._measurement == \"measurements\" and r._field == \"distance\") |> keep(columns: [\"_time\", \"_value\"])`
        })
    });
    
    const data = await response.text();
    return data;
}

// Function to format data for Chart.js
function formatData(influxData) {
    const lines = influxData.split('\r\n').slice(1); // Split lines and remove the header

    const pointData = lines.map(line => {
        const [db_timestamp, db_distance] = line.slice(11).split(',');
        return { x: Date.parse(db_timestamp), y: parseFloat(db_distance) };
    });

    return pointData;
}

// Function to create the initial graph
async function createGraph() {
    const startTime = new Date(document.getElementById('startTime').value).toISOString();
    const endTime = new Date(document.getElementById('endTime').value).toISOString();
    const influxData = await fetchData(startTime, endTime);
    const pointData = formatData(influxData);

    const data = {
        datasets: [{
            borderColor: 'rgba(75, 192, 192, 1)',
            borderWidth: 1,
            data: pointData,
            label: 'Distance',
            radius: 0,
        }]
    };

    const config = {
        type: 'line',
        data: data,
        options: {
            animation: false,
            parsing: false,
            interaction: {
                mode: 'nearest',
                axis: 'x',
                intersect: false
            },
            plugins: {
                decimation: {
                    enabled: false,
                },
            },
            scales: {
                x: {
                    type: 'time',
                    ticks: {
                        source: 'auto',
                        maxRotation: 0,
                        autoSkip: true,
                    }
                }
            }
        }
    };

    const ctx = document.getElementById('myChart').getContext('2d');
    myChart = new Chart(ctx, config);    
}

// Function to update the graph's data
async function updateGraph() {
    const startTime = new Date(document.getElementById('startTime').value).toISOString();
    const endTime = new Date(document.getElementById('endTime').value).toISOString();
    const influxData = await fetchData(startTime, endTime);
    const pointData = formatData(influxData);

    myChart.data.datasets[0].data = pointData; // Update the data
    myChart.update(); // Update the chart
}

// Function to initialize time range to last 5 hours
function initializeTimeRange() {
    const now = new Date();
    now.setMinutes(0, 0, 0);
    const fiveHoursAgo = new Date(now.getTime() - 5 * 60 * 60 * 1000);
    
    document.getElementById('endTime').value = now.toLocaleString('sv').replace(' ', 'T');
    document.getElementById('startTime').value = fiveHoursAgo.toLocaleString('sv').replace(' ', 'T');

    createGraph();
}

// Function to adjust the start or end time by 1 hour
function adjustTime(type, hours) {
    const time = new Date(document.getElementById(type + 'Time').value);
    time.setHours(time.getHours() + hours);
    document.getElementById(type + 'Time').value = time.toLocaleString('sv').replace(' ', 'T');
    
    updateGraph();
}

// Function to show the settings popup
function showSettings() {
    document.getElementById('settings-popup').style.display = 'block';
}

// Function to close the settings popup
function closeSettings() {
    document.getElementById('settings-popup').style.display = 'none';
}

// Function to show the graph popup
function showGraph() {
    document.getElementById('graph-popup').style.display = 'block';
}

// Function to close the graph popup
function closeGraph() {
    document.getElementById('graph-popup').style.display = 'none';
}

// Change the icons on hover
document.getElementById('settings-icon').onmouseover = function() {
    this.src = 'settings-hover.png';
}
document.getElementById('settings-icon').onmouseout = function() {
    this.src = 'settings.png';
}

document.getElementById('graph-icon').onmouseover = function() {
    this.src = 'chart-hover.png';
}
document.getElementById('graph-icon').onmouseout = function() {
    this.src = 'chart.png';
}

function servo(pos) {
    clearTimeout(timeout);
    timeout = setTimeout(function () {
        fetch('/set?position=' + pos, {
            method: 'GET',
            headers: {
                'Content-Type': 'application/json'
            },
        });
    }, 500);
}

function detachServo() {
    fetch('/set?servo=0', {
        method: 'GET',
        headers: {
            'Content-Type': 'application/json'
        },
    });
}

function sendPostRequest() {
    fetch('/', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
            // Add any other headers as needed
        },
        // Add body data if required
        // body: 
    })
    .then(response => {
        // Handle the response as needed
        console.log('POST request sent successfully');
    })
    .catch(error => {
        // Handle errors
        console.error('Error sending POST request:', error);
    });
}
