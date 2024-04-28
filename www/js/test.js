'use strict';

(() => {

const bt_btn = document.getElementById('bt-btn');
const rx_msg = document.getElementById('rx-msg');
const chunks = document.getElementById('chunks');
const msgs   = document.getElementById('msgs');
const rx_msg_max = parseInt(rx_msg.getAttribute('rows'));

const bt_svc_id  = 0xFFE0;
const bt_char_id = 0xFFE1;

let total_chunks = 0;
let bad_chunks   = 0;
let total_msgs   = 0;
let bad_msgs     = 0;

let bt_char    = null;
let rx_msgs    = [];
let last_tag   = null;
let msg_buff   = null;
let msg_start  = '#';
let msg_center = '_';

function initPage()
{
    if (!navigator.bluetooth) {
        document.body.innerHTML = '<div class="alert-page">The Bluetooth is not supported in this browser. Please try another one.</div>';
        return;
    }
    bt_btn.onclick = onConnect;
}

function showMessage(msg)
{
    if (rx_msgs.length >= rx_msg_max)
        rx_msgs.shift();
    rx_msgs.push(msg);
    console.log('rx:', msg);
    rx_msg.textContent = rx_msgs.join('\n');
}

function onDisconnection(event)
{
    const device = event.target;
    console.log(device.name + ' bluetooth device disconnected');
    rx_msg.disabled = true;
    bt_char = null;
    connectTo(device);
}

function on_new_msg(msg)
{
    total_msgs += 1;
    if (msg.length % 2) {
        console.log('invalid message length (' + msg.length + '): ' + msg)
        bad_msgs += 1;
        return;
    }
    if (msg.slice(msg.length/2, msg.length/2+1) != msg_center) {
        console.log('invalid message : ' + msg)
        bad_msgs += 1;
        return;
    }
    if (msg.slice(1, msg.length/2) != msg.slice(msg.length/2 + 1)) {
        console.log('corrupt message : ' + msg)
        bad_msgs += 1;
        return;
    }
}

function on_new_chunk(msg)
{
    const tag = msg.charCodeAt(0) - 'a'.charCodeAt(0);
    let bad_chunk = false;
    if (last_tag !== null) {
        let next_tag = last_tag + 1;
        if (next_tag > 15)
            next_tag = 0;
        if (tag != next_tag)
            bad_chunk = true;
    }
    last_tag = tag;

    if (bad_chunk) {
        bad_chunks += 1;
        bad_msgs += 1;
        msg_buff = null;
    }

    let start = 1, off = 1;
    for (;;) {
        const i = msg.indexOf(msg_start, off);
        if (i >= 0) {
            if (msg_buff !== null)
                on_new_msg(msg_buff + msg.slice(start, i));
            msg_buff = '';
            start = i;
            off = i + 1;
        } else {
            msg_buff += msg.slice(start);
            break;
        }
    }

    showMessage(msg);
    total_chunks += 1;

    chunks.innerHTML = total_chunks + ' / ' + bad_chunks;
    msgs.innerHTML   = total_msgs   + ' / ' + bad_msgs;
}

function onValueChanged(event)
{
    const value = event.target.value;
    let msg = '';
    for (let i = 0; i < value.byteLength; i++) {
        const c = value.getUint8(i);
        msg += String.fromCharCode(c);
    }
    on_new_chunk(msg);
}

function onBTConnected(device, characteristic)
{
    console.log(device.name, 'connected');
    characteristic.addEventListener('characteristicvaluechanged', onValueChanged);
    device.addEventListener('gattserverdisconnected', onDisconnection);
    rx_msg.disabled = false;
    bt_char = characteristic;
}

function connectTo(device)
{
    device.gatt.connect().
    then((server) => {
        console.log(device.name, 'GATT server connected, getting service...');
        return server.getPrimaryService(bt_svc_id);
    }).
    then((service) => {
        console.log(device.name, 'service found, getting characteristic...');
        return service.getCharacteristic(bt_char_id);
    }).
    then((characteristic) => {
        console.log(device.name, 'characteristic found');
        return characteristic.startNotifications().then(
            () => {
                onBTConnected(device, characteristic);
            },
            (err) => {
                console.log('Failed to subscribe to ' + device.name + ':', err.message);
                return Promise.reject(err);
            }
        );
    })
    .catch((err) => {
        console.log('Failed to connect to ' + device.name + ':', err.message);
        setTimeout(() => { connectTo(device); }, 500);
    });
}

function doConnect(devname)
{
    console.log('doConnect', devname);
    bt_btn.disabled = true;
    let filters = [{services: [bt_svc_id]}];
    if (devname) {
        filters.push({name: devname});
    }
    return navigator.bluetooth.requestDevice({
        filters: filters,
    }).
    then((device) => {
        console.log(device.name, 'selected');
        connectTo(device);
    })
    .catch((err) => {
        console.log('Failed to discover BT devices');
        bt_btn.disabled = false;
    });
}

function onConnect(event)
{
    console.log('onConnect');
    doConnect();
}

initPage();

})();

