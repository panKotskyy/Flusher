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
});

// Function to fetch initial values from the backend
async function fetchInitialValues() {
    const response = await fetch('/settings', {
        method: 'GET',
        headers: {
            'Content-Type': 'application/json'
        }
    });

    const settings = await response.json();
    console.log('Initial settings:', settings);

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
async function fetchData() {
    const response = await fetch('https://us-east-1-1.aws.cloud2.influxdata.com/api/v2/query', {
        method: 'POST',
        headers: {
            'Authorization': 'Token OrddY9ea5LGv5bpZNEdnYrBBn7-4nwL1zrZI_5W-XVYghPNeIbh6a7J-1uNnfzaQsk7E78JHdX7l6ypsZNrXBg==',
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            "query": "from(bucket:\"Flusher\") |> range(start: -1d) |> filter(fn: (r) => r._measurement == \"measurements\" and r._field == \"distance\") |> keep(columns: [\"_time\", \"_value\"])"
        })
    });
    console.log(response);
    const data = await response.text();
    console.log("response", data);
    return data;
}

// Function to format data for Chart.js
function formatData(influxData) {
    const NUM_POINTS = 24 * 60 * 60;
    const now = Math.round(Date.now() / 1000) * 1000;
    const start = now - NUM_POINTS * 1000;

    const lines = influxData.split('\r\n').slice(1); // Split lines and remove the header
    console.log("lines", lines);
    const tm = Date.parse(lines[0].slice(11).split(',')[0]);
    console.log(now, start, tm, now - tm, start - tm, NUM_POINTS);

    const pointData = [];
    pointData.push({ x: start, y: 55 });

    lines.forEach((line) => {
        const [db_timestamp, db_distance] = line.slice(11).split(',');
        pointData.push({ x: Date.parse(db_timestamp), y: parseFloat(db_distance) });
    });

    return pointData;
}

// Function to create and render the graph
async function renderGraph() {
    const influxData = await fetchData();
    const pointData = formatData(influxData);

    console.log("pointData", pointData);

    const decimation = {
        enabled: false,
        // algorithm: 'min-max',
    };

    const data = {
        datasets: [{
            borderColor: 'rgba(75, 192, 192, 1)',
            borderWidth: 1,
            data: pointData,
            label: 'Large Dataset',
            radius: 0,
        }]
    };

    const config = {
        type: 'line',
        data: data,
        options: {
            // Turn off animations and data parsing for performance
            animation: false,
            parsing: false,

            interaction: {
                mode: 'nearest',
                axis: 'x',
                intersect: false
            },
            plugins: {
                decimation: decimation,
            },
            scales: {
                x: {
                    type: 'time',
                    ticks: {
                        source: 'auto',
                        // Disabled rotation for performance
                        maxRotation: 0,
                        autoSkip: true,
                    }
                }
            }
        }
    };

    const ctx = document.getElementById('myChart').getContext('2d');
    const myChart = new Chart(ctx, config);
}

// Call the renderGraph function
// renderGraph();

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
