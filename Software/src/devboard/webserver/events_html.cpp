#include "events_html.h"

const char events_html[] = R"rawliteral(<!doctype html><html><head><meta charset="utf-8"><title>Battery Emulator - Events</title><meta content="width=device-width" name="viewport">
<style>
body{background-color:#000;color:#fff;font-family:Arial;max-width:800px;margin:0 auto}
.event-log{display:flex;flex-direction:column}
.event{display:flex;flex-wrap:wrap;border:1px solid #fff;padding:10px}
.event>div{flex:1;min-width:100px;word-break:break-word}
.event:nth-child(even){background-color:#455a64}
.event:nth-child(odd){background-color:#394b52}
button{background-color:#505E67;color:white;border:none;padding:10px 20px;margin-bottom:20px;cursor:pointer;border-radius:10px}
button:hover{background-color:#3A4A52}
</style></head><body>
<div style="background-color:#303e47;padding:10px;margin-bottom:10px;border-radius:25px">
<div class="event-log" id="event-log">
<div class="event" style="background-color:#1e2c33;font-weight:700"><div>Event Type</div><div>Severity</div><div>Last Event</div><div>Count</div><div>Data</div><div>Message</div></div>
</div></div>
<button onclick="askClear()">Clear all events</button>
<button onclick="location.href='/'">Back to main page</button>
<script>
function update(){
  fetch('/api/events').then(function(r){return r.json();}).then(function(events){
    var log=document.getElementById('event-log');
    var header=log.firstElementChild;
    while(log.children.length>1)log.removeChild(log.lastChild);
    for(var i=0;i<events.length;i++){
      var e=events[i];
      var row=document.createElement('div');
      row.className='event';
      row.innerHTML='<div>'+e.type+'</div><div>'+e.level+'</div><div>'+new Date(Date.now()-e.ms_ago).toLocaleString()+'</div><div>'+e.occurrences+'</div><div>'+e.data+'</div><div>'+e.message+'</div>';
      log.appendChild(row);
    }
  }).catch(function(err){console.error('Failed to fetch events:',err);});
}
function askClear(){if(window.confirm('Are you sure you want to clear all events?'))window.location.href='/clearevents';}
update();
setInterval(update,10000);
</script></body></html>)rawliteral";
