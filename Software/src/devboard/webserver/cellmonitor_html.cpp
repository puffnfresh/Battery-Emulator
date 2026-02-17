#include "cellmonitor_html.h"

const char cellmonitor_html[] = R"rawliteral(<!doctype html><html><head><meta charset="utf-8"><title>Battery Emulator - Cell Monitor</title><meta content="width=device-width" name="viewport">
<style>
body{background-color:#000;color:#fff;font-family:Arial;max-width:800px;margin:0 auto;text-align:center}
button{background-color:#505E67;color:#fff;border:none;padding:10px 20px;margin-bottom:20px;cursor:pointer;border-radius:10px}
button:hover{background-color:#3A4A52}
.container{display:flex;flex-wrap:wrap;justify-content:space-around}
.cell{padding:10px;border:1px solid #fff;text-align:center}
.low-voltage{color:red}
.voltage-values{margin-bottom:10px}
.graph{display:flex;align-items:flex-end;height:200px;border:1px solid #ccc;position:relative}
.bar{margin:0;background-color:blue;display:inline-block;position:relative;cursor:pointer;border:1px solid #fff}
.value-display{text-align:left;font-weight:bold;margin-top:10px}
.bat-block{padding:10px;margin-bottom:10px;border-radius:50px}
.legend-badge{font-weight:bold;padding:2px 8px;border-radius:4px;margin-right:15px}
</style></head><body>
<button onclick="location.href='/'">Back to main page</button>
<div id="bat1" class="bat-block" style="background-color:#303E47"></div>
<div id="bat2" class="bat-block" style="background-color:#303E41;display:none"></div>
<div id="bat3" class="bat-block" style="background-color:#313e41;display:none"></div>
<button onclick="location.href='/'">Back to main page</button>
<script>
var BALANCING_STATUS_ACTIVE=3;
function mapRange(v,fL,fH,tL,tH){return(v-fL)*(tH-tL)/(fH-fL)+tL;}

function renderBattery(container,voltages,balancingArr,balancingStatus,label){
  container.innerHTML='';
  var data=[];var bal=[];
  for(var i=0;i<voltages.length;i++){
    if(voltages[i]===0)continue;
    data.push(voltages[i]);
    bal.push(!!balancingArr[i]);
  }

  var voltDiv=document.createElement('div');
  voltDiv.className='voltage-values';
  var cellDiv=document.createElement('div');
  cellDiv.className='container';
  var graphDiv=document.createElement('div');
  graphDiv.className='graph';
  var valDisp=document.createElement('div');
  valDisp.className='value-display';
  valDisp.textContent='Value: ...';

  if(data.length===0){
    voltDiv.textContent=label?label+': No cell voltage data available':'No cell voltage data available';
    container.appendChild(voltDiv);
    return;
  }

  var minV=Math.min.apply(null,data);
  var maxV=Math.max.apply(null,data);
  var minIdx=data.indexOf(minV);
  var maxIdx=data.indexOf(maxV);
  var dev=maxV-minV;
  var minScale=minV-20;
  var maxScale=maxV+20;

  var hdr=(label?label+'<br>':'')+'Max Voltage: '+maxV+' mV<br>Min Voltage: '+minV+' mV<br>Voltage Deviation: '+dev+' mV';
  var hasPerCellBal=false;
  for(var i=0;i<bal.length;i++){if(bal[i]){hasPerCellBal=true;break;}}
  if(hasPerCellBal){
    hdr+=' (Cells are balancing)';
  }else if(balancingStatus===BALANCING_STATUS_ACTIVE){
    hdr+=' (Battery is balancing now!)';
  }
  voltDiv.innerHTML=hdr;

  var suffix='_'+container.id;
  for(var i=0;i<data.length;i++){
    (function(idx){
      var mV=data[idx];
      var cell=document.createElement('div');
      cell.className='cell';
      cell.id='cell'+suffix+'_'+idx;
      var txt='Cell '+(idx+1)+'<br>'+mV+' mV';
      if(mV<3000)txt="<span class='low-voltage'>"+txt+"</span>";
      cell.innerHTML=txt;

      var bar=document.createElement('div');
      bar.className='bar';
      bar.id='bar'+suffix+'_'+idx;
      var h=mapRange(mV,minScale,maxScale,20,200);
      bar.style.height=h+'px';
      bar.style.width=(750/data.length)+'px';
      if(bal[idx]){
        bar.style.backgroundColor='#00FFFF';
        bar.style.borderColor='#00FFFF';
      }else{
        bar.style.backgroundColor='blue';
        bar.style.borderColor='white';
      }

      if(idx===minIdx||idx===maxIdx){
        cell.style.borderColor='red';
        bar.style.borderColor='red';
      }

      bar.addEventListener('mouseenter',function(){
        valDisp.textContent='Value: '+mV+(bal[idx]?' (balancing)':'');
        bar.style.backgroundColor=bal[idx]?'#80FFFF':'lightblue';
        cell.style.backgroundColor=bal[idx]?'#006666':'blue';
      });
      bar.addEventListener('mouseleave',function(){
        valDisp.textContent='Value: ...';
        bar.style.backgroundColor=bal[idx]?'#00FFFF':'blue';
        cell.style.removeProperty('background-color');
      });
      cell.addEventListener('mouseenter',function(){
        valDisp.textContent='Value: '+mV;
        bar.style.backgroundColor=bal[idx]?'#80FFFF':'lightblue';
        cell.style.backgroundColor=bal[idx]?'#006666':'blue';
      });
      cell.addEventListener('mouseleave',function(){
        bar.style.backgroundColor=bal[idx]?'#00FFFF':'blue';
        cell.style.removeProperty('background-color');
      });

      cellDiv.appendChild(cell);
      graphDiv.appendChild(bar);
    })(i);
  }

  var legend=document.createElement('div');
  legend.style.marginTop='5px';
  legend.innerHTML="<span class='legend-badge' style='color:#fff;background-color:blue'>Idle</span>";
  if(hasPerCellBal){
    legend.innerHTML+="<span class='legend-badge' style='color:#000;background-color:#00FFFF'>Balancing</span>";
  }else if(balancingStatus===BALANCING_STATUS_ACTIVE){
    legend.innerHTML+="<span class='legend-badge' style='color:#000;background-color:#ff9900'>Balancing is active now!</span>";
  }
  legend.innerHTML+="<span class='legend-badge' style='color:#fff;background-color:red'>Min/Max</span>";

  container.appendChild(voltDiv);
  container.appendChild(cellDiv);
  container.appendChild(graphDiv);
  container.appendChild(valDisp);
  container.appendChild(legend);
}

function update(){
  fetch('/api/cells').then(function(r){return r.json();}).then(function(d){
    var b1=document.getElementById('bat1');
    var b2=document.getElementById('bat2');
    var b3=document.getElementById('bat3');

    if(d.battery){
      b1.style.display='';
      renderBattery(b1,d.battery.voltages_mV,d.battery.balancing,d.battery.balancing_status,null);
    }

    if(d.battery2){
      b2.style.display='';
      renderBattery(b2,d.battery2.voltages_mV,d.battery2.balancing,d.battery2.balancing_status,'Battery #2');
    }else{
      b2.style.display='none';
    }

    if(d.battery3){
      b3.style.display='';
      renderBattery(b3,d.battery3.voltages_mV,d.battery3.balancing,d.battery3.balancing_status,'Battery #3');
    }else{
      b3.style.display='none';
    }
  }).catch(function(e){console.error('Failed to fetch cell data:',e);});
}

update();
setInterval(update,10000);
</script></body></html>)rawliteral";
