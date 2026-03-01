#pragma once
#if defined(ARDUINO)
#include <Arduino.h>

namespace web_ui {

// CSS stored in PROGMEM to avoid heap allocation
static const char kCss[] PROGMEM = R"css(
:root{--bg:#111827;--bg-h:#005782;--bg-h2:#003d5c;--bg-c:#1F2937;--bg-i:#374151;
--tx:#F9FAFB;--tx2:#9CA3AF;--ac:#4F46E5;--ach:#4338CA;--bd:#374151;
--ok:#10B981;--wn:#F59E0B;--er:#EF4444}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg);color:var(--tx);
font-size:14px;min-height:100vh}
.hdr{background:linear-gradient(135deg,var(--bg-h),var(--bg-h2));padding:1rem 1.5rem;
display:flex;align-items:center;gap:0.75rem}
.hdr h1{font-size:1.25rem;font-weight:600}
.hdr .chip{font-size:0.7rem;background:rgba(255,255,255,0.15);padding:0.2rem 0.5rem;
border-radius:9999px}
.tabs{display:flex;overflow-x:auto;background:var(--bg-c);border-bottom:2px solid var(--bd);
-webkit-overflow-scrolling:touch;scrollbar-width:none}
.tabs::-webkit-scrollbar{display:none}
.tab{padding:0.65rem 1rem;border:none;background:transparent;color:var(--tx2);cursor:pointer;
white-space:nowrap;font-size:0.8rem;border-bottom:2px solid transparent;
margin-bottom:-2px;transition:color 0.15s,border-color 0.15s}
.tab.on{color:var(--tx);border-bottom-color:var(--ac)}
.tp{display:none;padding:1rem;max-width:640px;margin:0 auto}
.tp.on{display:block}
.card{background:var(--bg-c);border-radius:0.5rem;padding:1rem;margin-bottom:1rem}
.card h3{font-size:0.9rem;margin-bottom:0.75rem;color:var(--tx2)}
.fg{margin-bottom:0.75rem}
.fg label{display:block;font-size:0.75rem;color:var(--tx2);margin-bottom:0.2rem}
.fg input,.fg select{width:100%;padding:0.45rem 0.5rem;border:1px solid var(--bd);
border-radius:0.375rem;background:var(--bg-i);color:var(--tx);font-size:0.85rem}
.fg input:focus,.fg select:focus{outline:none;border-color:var(--ac)}
.fg .ht{font-size:0.65rem;color:var(--tx2);margin-top:0.15rem}
.btn{padding:0.5rem 1.5rem;border:none;border-radius:0.375rem;cursor:pointer;
font-size:0.85rem;font-weight:500;transition:background 0.15s}
.btn-p{background:var(--ac);color:#fff}
.btn-p:hover{background:var(--ach)}
.btn-d{background:var(--er);color:#fff}
.btn-d:hover{background:#DC2626}
.sg{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:0.6rem}
.si{background:var(--bg-i);border-radius:0.375rem;padding:0.6rem}
.si .sl{font-size:0.65rem;color:var(--tx2);text-transform:uppercase;letter-spacing:0.05em}
.si .sv{font-size:1rem;font-weight:600;margin-top:0.15rem;word-break:break-all}
.c-ok{color:var(--ok)}.c-wn{color:var(--wn)}.c-er{color:var(--er)}
.toast{position:fixed;bottom:1rem;right:1rem;left:1rem;max-width:400px;margin-left:auto;
padding:0.75rem 1rem;border-radius:0.5rem;background:var(--ok);color:#fff;
transform:translateY(calc(100% + 2rem));transition:transform 0.3s;z-index:100;
font-size:0.85rem}
.toast.show{transform:translateY(0)}
.toast.err{background:var(--er)}
.mt{margin-top:0.75rem}
)css";

// JavaScript stored in PROGMEM
static const char kJs[] PROGMEM = R"js(
function showTab(id){
  document.querySelectorAll('.tab').forEach(function(b){b.classList.remove('on')});
  document.querySelectorAll('.tp').forEach(function(t){t.classList.remove('on')});
  var btn=document.querySelector('[data-t="'+id+'"]');
  var pane=document.getElementById('t-'+id);
  if(btn)btn.classList.add('on');
  if(pane)pane.classList.add('on');
  try{localStorage.setItem('tab',id)}catch(e){}
  if(id==='status')startStatus();else stopStatus();
}
var _si=null;
function startStatus(){
  refreshStatus();
  if(!_si)_si=setInterval(refreshStatus,5000);
}
function stopStatus(){
  if(_si){clearInterval(_si);_si=null}
}
function fmtTime(ms){
  var s=Math.floor(ms/1000);
  if(s<60)return s+'s';
  var m=Math.floor(s/60);s=s%60;
  if(m<60)return m+'m '+s+'s';
  var h=Math.floor(m/60);m=m%60;
  if(h<24)return h+'h '+m+'m '+s+'s';
  var d=Math.floor(h/24);h=h%24;
  return d+'d '+h+'h '+m+'m';
}
function pickMac(fieldName){
  var inp=document.querySelector('[name="'+fieldName+'"]');
  if(!inp)return;
  var wrap=inp.parentElement;
  var old=wrap.querySelector('.mac-dd');
  if(old){old.remove();return;}
  var dd=document.createElement('div');
  dd.className='mac-dd';
  dd.style.cssText='position:absolute;top:100%;left:0;right:0;z-index:99;background:var(--bg);border:1px solid var(--bd);border-radius:0.3rem;max-height:12rem;overflow-y:auto;margin-top:0.2rem;box-shadow:0 2px 8px rgba(0,0,0,0.2)';
  dd.innerHTML='<div style="padding:0.5rem;color:var(--st)">Loading\u2026</div>';
  wrap.appendChild(dd);
  var dismiss=function(e){if(!wrap.contains(e.target)){dd.remove();document.removeEventListener('click',dismiss,true);}};
  setTimeout(function(){document.addEventListener('click',dismiss,true);},0);
  fetch('/devices').then(function(r){return r.json()}).then(function(arr){
    dd.innerHTML='';
    if(!arr.length){dd.innerHTML='<div style="padding:0.5rem;color:var(--st)">No devices found</div>';return;}
    arr.forEach(function(dev){
      var row=document.createElement('div');
      row.style.cssText='padding:0.45rem 0.6rem;cursor:pointer';
      row.onmouseover=function(){row.style.background='var(--ac)';row.style.color='#fff'};
      row.onmouseout=function(){row.style.background='';row.style.color=''};
      row.textContent=dev.name+' ('+dev.type+') - '+dev.mac;
      row.onclick=function(){inp.value=dev.mac;dd.remove();document.removeEventListener('click',dismiss,true);};
      dd.appendChild(row);
    });
  }).catch(function(){dd.innerHTML='<div style="padding:0.5rem;color:var(--er)">Failed to load devices</div>';});
}
function appendMac(fieldName){
  var inp=document.querySelector('[name="'+fieldName+'"]');
  if(!inp)return;
  fetch('/devices').then(function(r){return r.json()}).then(function(arr){
    if(!arr.length){toast('No devices found','err');return;}
    var sel=prompt(arr.map(function(d,i){return(i+1)+'. '+d.name+' ('+d.type+') - '+d.mac}).join('\n')+'\n\nEnter number:');
    if(!sel)return;
    var idx=parseInt(sel,10)-1;
    if(idx<0||idx>=arr.length)return;
    var mac=arr[idx].mac;
    var cur=inp.value.trim();
    inp.value=cur?(cur+','+mac):mac;
  }).catch(function(){});
}
function refreshStatus(){
  fetch('/status').then(function(r){return r.json()}).then(function(d){
    for(var k in d){
      var el=document.getElementById('st-'+k);
      if(!el)continue;
      var v=d[k];
      if(k==='uptime_ms'){el.textContent=fmtTime(v);continue;}
      if(k==='heartbeat_last_seen_ms'){
        if(v>0&&d.uptime_ms){var ago=d.uptime_ms-v;el.textContent=fmtTime(ago)+' ago';}
        else el.textContent='-';
        continue;
      }
      if(k==='furnace_state'&&d.furnace_state_text){el.textContent=d.furnace_state_text;continue;}
      if(k==='free_heap'){el.textContent=Number(v).toLocaleString()+' B';continue;}
      if(v===null||v===undefined)el.textContent='-';
      else if(typeof v==='boolean')el.textContent=v?'Yes':'No';
      else el.textContent=String(v);
    }
  }).catch(function(){});
}
function submitForm(f){
  var fd=new FormData(f);
  fetch('/config',{method:'POST',body:fd}).then(function(r){return r.text()}).then(function(t){
    if(t.indexOf('reboot')>=0)toast('Saved. Reboot required for some changes.','wn');
    else toast('Settings saved.');
  }).catch(function(e){toast('Error: '+e,'err')});
  return false;
}
function toast(m,t){
  var el=document.getElementById('toast');
  el.textContent=m;
  el.className='toast show'+(t==='err'?' err':'');
  setTimeout(function(){el.className='toast'},3000);
}
document.addEventListener('DOMContentLoaded',function(){
  try{var s=localStorage.getItem('tab');
  if(s&&document.getElementById('t-'+s))showTab(s);
  else{var first=document.querySelector('.tab');if(first)showTab(first.getAttribute('data-t'))}
  }catch(e){}
});
)js";

// Begin the page: writes <html>, <head>, CSS, </head>, <body>, header, tab bar
// tab_names is an array of {id, label} pairs
struct TabDef {
  const char *id;
  const char *label;
};

inline void page_begin(String &html, const char *title, const char *subtitle,
                        const TabDef *tabs, size_t tab_count) {
  html += F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>");
  html += title;
  html += F("</title><style>");
  html += FPSTR(kCss);
  html += F("</style></head><body>");

  // Header
  html += F("<div class=\"hdr\"><h1>");
  html += title;
  html += F("</h1>");
  if (subtitle && subtitle[0]) {
    html += F("<span class=\"chip\">");
    html += subtitle;
    html += F("</span>");
  }
  html += F("</div>");

  // Tab bar
  html += F("<nav class=\"tabs\">");
  for (size_t i = 0; i < tab_count; ++i) {
    html += F("<button class=\"tab");
    if (i == 0) html += F(" on");
    html += F("\" data-t=\"");
    html += tabs[i].id;
    html += F("\" onclick=\"showTab('");
    html += tabs[i].id;
    html += F("')\">");
    html += tabs[i].label;
    html += F("</button>");
  }
  html += F("</nav>");
}

// Begin a tab pane
inline void tab_begin(String &html, const char *id, bool first = false) {
  html += F("<div class=\"tp");
  if (first) html += F(" on");
  html += F("\" id=\"t-");
  html += id;
  html += F("\">");
}

// End a tab pane
inline void tab_end(String &html) {
  html += F("</div>");
}

// End the page: toast div, JS, close body/html
inline void page_end(String &html) {
  html += F("<div id=\"toast\" class=\"toast\"></div>");
  html += F("<script>");
  html += FPSTR(kJs);
  html += F("</script></body></html>");
}

}  // namespace web_ui
#endif  // ARDUINO
