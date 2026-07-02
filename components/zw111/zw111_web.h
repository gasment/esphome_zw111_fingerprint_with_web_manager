#pragma once

static const char ZW111_HTML[] = R"rawliteral(<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no"><title>ZW111 指纹管理</title><style>
:root{--bg:#0a0e17;--panel:#111827;--accent:#1e2d4a;--highlight:#3b82f6;--green:#10b981;--text:#e2e8f0;--muted:#6b7f99;--border:#1e2938;--p:14px;--ps:10px;--pxs:7px;--fs:15px;--fs-sm:13px;--fs-xs:11px;--h:34px;--rad:8px}
@media(min-width:600px){:root{--p:16px;--ps:12px;--pxs:10px;--fs:16px;--fs-sm:14px;--fs-xs:12px;--h:36px;--rad:10px}}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;max-width:100vw;overflow-x:hidden}
.app{max-width:600px;margin:0 auto;width:100%}
.header{background:var(--panel);padding:var(--p);border-bottom:2px solid var(--highlight);display:flex;align-items:center;gap:var(--ps)}
.header h1{font-size:var(--fs);font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.dot{width:10px;height:10px;border-radius:50%;background:var(--green);animation:pulse 2s infinite;flex-shrink:0}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.tabs{display:flex;background:var(--panel);padding:0 var(--p)}
.tabs button{flex:1;padding:var(--ps) var(--pxs);border:none;background:transparent;color:var(--muted);font-size:var(--fs-sm);cursor:pointer;border-bottom:3px solid transparent;transition:.2s}
.tabs button.active{color:var(--highlight);border-bottom-color:var(--highlight)}
.tab-content{display:none;padding:var(--p)}
.tab-content.active{display:block}
.card{background:var(--panel);border:1px solid var(--border);border-radius:var(--rad);padding:var(--p);margin-bottom:var(--p);max-width:100%}
.card h3{font-size:var(--fs-sm);margin-bottom:var(--ps);color:var(--highlight)}
.info-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:var(--pxs) var(--ps)}
@media(max-width:400px){.info-grid{grid-template-columns:1fr}}
.info-item{display:flex;justify-content:space-between;padding:var(--pxs) 0;border-bottom:1px solid var(--border);min-width:0}
.info-label{color:var(--muted);font-size:var(--fs-xs);flex-shrink:0;margin-right:var(--pxs)}
.info-value{font-weight:600;font-size:var(--fs-xs);text-align:right;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.btn{padding:var(--ps) var(--p);border:none;border-radius:var(--rad);font-size:var(--fs-sm);cursor:pointer;font-weight:600;transition:.2s}
.btn-primary{background:var(--highlight);color:#fff}.btn-primary:hover{opacity:.85}
.btn-outline{background:transparent;border:1px solid var(--border);color:var(--text)}.btn-outline:hover{border-color:var(--highlight)}
.action-row{display:flex;gap:var(--ps);flex-wrap:wrap;align-items:center}
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);padding:var(--ps) var(--p);border-radius:var(--rad);font-weight:600;z-index:9999;animation:fadeOut 3s forwards;font-size:var(--fs-xs);white-space:nowrap;max-width:90vw}
.toast-ok{background:var(--green);color:#fff}
.toast-err{background:#c0392b;color:#fff}
.toast-info{background:var(--accent);color:#fff}
@keyframes fadeOut{0%,70%{opacity:1}100%{opacity:0}}
.fp-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:var(--pxs)}
@media(max-width:420px){.fp-grid{grid-template-columns:repeat(3,1fr)}}
@media(max-width:340px){.fp-grid{grid-template-columns:repeat(2,1fr)}}
.fp-item{background:var(--bg);border:1px solid var(--border);border-radius:var(--rad);padding:var(--pxs);text-align:center;cursor:pointer;transition:.2s;min-height:80px;display:flex;flex-direction:column;align-items:center;justify-content:center;min-width:0}
@media(max-width:420px){.fp-item{min-height:72px}}
.fp-item:hover{border-color:var(--highlight);background:rgba(59,130,246,.05)}
.fp-avatar{width:var(--h);height:var(--h);margin-bottom:var(--pxs);border-radius:50%;background:var(--accent);display:flex;align-items:center;justify-content:center;transition:background .3s;flex-shrink:0}
.fp-avatar.enrolled{background:#2ecc71}
.fp-avatar.enrolled svg{fill:#fff}
.fp-avatar svg{width:calc(var(--h)*.55);height:calc(var(--h)*.55);fill:var(--muted)}
.fp-id{font-size:var(--fs-xs);color:var(--muted);max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fp-name{font-size:calc(var(--fs-xs)*1.05);color:var(--text);margin-top:var(--pxs);max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.pagination{display:flex;justify-content:center;align-items:center;gap:var(--pxs);margin-top:var(--p);flex-wrap:wrap}
.pagination button{min-width:var(--h);height:var(--h);border:none;border-radius:var(--rad);background:var(--accent);color:var(--text);font-size:var(--fs-xs);cursor:pointer}
.pagination button.active{background:var(--highlight)}
.modal-overlay{position:fixed;top:0;left:0;width:100vw;height:100vh;background:rgba(0,0,0,.6);z-index:1000;display:none;align-items:center;justify-content:center;max-width:100vw;overflow:hidden}
.modal-overlay.active{display:flex}
.modal{background:var(--panel);border:1px solid var(--border);border-radius:var(--rad);padding:var(--p);width:92%;max-width:340px;text-align:center;max-height:90vh;overflow-y:auto}
.modal h3{font-size:var(--fs);color:var(--text);margin-bottom:var(--p)}
.modal .fp-avatar{margin:0 auto var(--ps);width:calc(var(--h)*1.4);height:calc(var(--h)*1.4)}
.modal .fp-avatar svg{width:calc(var(--h)*.8);height:calc(var(--h)*.8)}
.modal-btn{display:block;width:100%;margin:var(--pxs) 0;padding:var(--ps);border:1px solid var(--border);border-radius:var(--rad);background:var(--bg);color:var(--text);font-size:var(--fs-sm);cursor:pointer;text-align:center;transition:.2s}
.modal-btn:hover{border-color:var(--highlight);background:#1a1f3a}
.modal-btn.danger{color:#c0392b}
.modal-btn.danger:hover{border-color:#c0392b}
.enroll-modal .enroll-icon{width:70px;height:70px;margin:0 auto var(--p);position:relative}
.enroll-modal .enroll-icon svg{width:70px;height:70px}
@media(max-width:420px){.enroll-modal .enroll-icon{width:56px;height:56px}.enroll-modal .enroll-icon svg{width:56px;height:56px}}
.enroll-icon-ring{fill:none;stroke:var(--border);stroke-width:4}
.enroll-icon-finger{fill:var(--muted);transition:fill .4s}
.enroll-icon-ring.active{stroke:var(--green);animation:ringPulse 1.5s infinite}
.enroll-icon-finger.active{fill:#61BC77}
.enroll-icon-ring.error{stroke:#c0392b}
.enroll-icon-finger.error{fill:#c0392b}
.enroll-icon-ring.success{stroke:var(--green)}
.enroll-icon-finger.success{fill:var(--green)}
.enroll-icon-ring.waiting{stroke:var(--highlight);animation:ringSpin 1s linear infinite}
@keyframes ringPulse{0%,100%{stroke-opacity:1;r:30}50%{stroke-opacity:.3;r:34}}
@keyframes ringSpin{0%{stroke-dasharray:40 180;stroke-dashoffset:0}50%{stroke-dasharray:80 140;stroke-dashoffset:-20}100%{stroke-dasharray:40 180;stroke-dashoffset:-220}}
.enroll-modal .enroll-msg{font-size:var(--fs-sm);color:var(--highlight);margin-bottom:var(--pxs);min-height:20px}
.enroll-modal .enroll-detail{font-size:var(--fs-xs);color:var(--muted);margin-bottom:var(--pxs);min-height:16px}
.enroll-modal .enroll-step{font-size:var(--fs-xs);color:var(--muted);margin-bottom:var(--p)}
.enroll-modal .enroll-cancel-btn{display:block;width:100%;padding:var(--ps);border:1px solid var(--border);border-radius:var(--rad);background:transparent;color:#c0392b;font-size:var(--fs-sm);cursor:pointer;margin-top:var(--pxs)}
.enroll-modal .enroll-cancel-btn:hover{border-color:#c0392b;background:rgba(192,57,43,.1)}
.note-editor{display:none;margin-top:var(--ps);padding-top:var(--ps);border-top:1px solid var(--border);text-align:left}
.note-editor.active{display:block}
.note-editor input{width:100%;padding:var(--ps);border:1px solid var(--border);border-radius:var(--rad);background:var(--bg);color:var(--text);font-size:var(--fs-sm);margin-bottom:var(--pxs);max-width:100%}
.note-editor .note-status{font-size:var(--fs-xs);color:var(--green);min-height:18px;margin-bottom:var(--pxs)}
.note-editor .btn{font-size:var(--fs-xs);padding:var(--pxs) var(--ps)}
.setting-row{display:flex;align-items:center;justify-content:space-between;padding:var(--pxs) 0;border-bottom:1px solid var(--border);flex-wrap:wrap;gap:var(--pxs)}
.setting-row:last-child{border-bottom:none}
.setting-row .s-label{color:var(--muted);font-size:var(--fs-xs);flex-shrink:0}
.setting-row .s-input{background:var(--bg);border:1px solid var(--border);border-radius:var(--rad);color:var(--text);font-size:var(--fs-sm);padding:var(--pxs) var(--ps);width:60px;text-align:center;max-width:100%}
.setting-row .s-input.wide{width:70px}
.setting-row .s-hint{color:var(--muted);font-size:var(--fs-xs);margin-right:var(--pxs)}
.setting-row input[type=checkbox]{width:18px;height:18px;accent-color:var(--highlight);cursor:pointer}
.setting-row .s-right{display:flex;align-items:center;gap:var(--pxs);margin-left:auto}
</style></head><body>
<div class="app">
<div class="header"><div class="dot" id="statusDot"></div><h1>ZW111 指纹识别系统</h1></div>
<div class="tabs"><button class="active" onclick="switchTab('fingerprint')">指纹管理</button><button onclick="switchTab('settings')">设置/关于</button></div>

<div id="tab-fingerprint" class="tab-content active">
<div class="card"><div class="fp-grid" id="fpGrid"></div><div class="pagination" id="pagination"></div></div>
<div class="card"><h3>操作</h3><div class="action-row"><button class="btn btn-primary" onclick="showBatchDelete()">批量删除</button></div></div>
</div>

<div id="tab-settings" class="tab-content">
<div class="card"><h3>模块信息</h3><div class="info-grid" id="moduleInfo"></div></div>
<div class="card"><h3>系统设置</h3>
<div class="setting-row"><span class="s-label">指纹验证置信度</span><div class="s-right"><span class="s-hint">1-5</span><input class="s-input" type="number" id="setScoreLevel" min="1" max="5" onchange="onSettingChange('score_level',this.value)"></div></div>
<div class="setting-row"><span class="s-label">录入时采集次数</span><div class="s-right"><span class="s-hint">1-8</span><input class="s-input" type="number" id="setEnrollMax" min="1" max="8" onchange="onSettingChange('enroll_max',this.value)"></div></div>
<div class="setting-row"><span class="s-label">禁止重复注册</span><div class="s-right"><input type="checkbox" id="setDupBlock" onchange="onSettingChange('dup_block',this.checked?1:0)"></div></div>
</div></div>
</div>
<div id="toast"></div>

<div class="modal-overlay" id="loginOverlay" style="background:var(--bg)">
<div class="modal" style="max-width:320px">
<h3 style="margin-bottom:var(--p)">ZW111 登陆</h3>
<input id="loginUser" placeholder="用户名" style="width:100%;padding:var(--ps);margin-bottom:var(--ps);border:1px solid var(--border);border-radius:var(--rad);background:var(--bg);color:var(--text);font-size:var(--fs-sm);box-sizing:border-box">
<input id="loginPass" type="password" placeholder="密码" style="width:100%;padding:var(--ps);margin-bottom:var(--p);border:1px solid var(--border);border-radius:var(--rad);background:var(--bg);color:var(--text);font-size:var(--fs-sm);box-sizing:border-box">
<button onclick="doLogin()" style="width:100%;padding:var(--ps);border:none;border-radius:var(--rad);background:var(--highlight);color:#fff;font-size:var(--fs-sm);font-weight:600;cursor:pointer">登陆</button>
<p id="loginError" style="color:#c0392b;font-size:var(--fs-xs);margin-top:var(--ps);min-height:20px"></p>
</div></div>

<div class="modal-overlay" id="modalOverlay" onclick="closeModal(event)">
<div class="modal" onclick="event.stopPropagation()">
<div class="fp-avatar" id="modalAvatar"><svg viewBox="0 0 24 24"><path d="M12 12c2.7 0 4.8-2.1 4.8-4.8S14.7 2.4 12 2.4 7.2 4.5 7.2 7.2 9.3 12 12 12zm0 2.4c-3.2 0-9.6 1.6-9.6 4.8v1.2c0 .66.54 1.2 1.2 1.2h16.8c.66 0 1.2-.54 1.2-1.2v-1.2c0-3.2-6.4-4.8-9.6-4.8z"/></svg></div>
<h3 id="modalTitle">ID: 0</h3>
<button class="modal-btn" id="btnEnroll" onclick="startEnroll()">录入指纹</button>
<button class="modal-btn" onclick="modalNote()">备注</button>
<button class="modal-btn danger" id="btnDelete" onclick="modalDelete()">删除</button>
<div class="note-editor" id="noteEditor">
<input type="text" id="noteInput" maxlength="32" placeholder="输入备注 (字母/数字)">
<div class="note-status" id="noteStatus"></div>
<button class="btn btn-primary" onclick="saveNote()">保存</button>
<button class="btn btn-outline" onclick="cancelNote()">取消</button>
</div>
</div></div>

<div class="modal-overlay" id="deleteOverlay" onclick="closeDeleteOverlay(event)">
<div class="modal" onclick="event.stopPropagation()">
<h3 style="color:#c0392b">确认删除</h3>
<p style="color:var(--muted);font-size:var(--fs-sm);margin:var(--pxs) 0 var(--p)" id="deleteMsg">将删除 ID:0 的指纹模板及备注信息，此操作不可恢复。</p>
<button class="modal-btn danger" onclick="confirmDelete()">确认删除</button>
<button class="modal-btn" style="color:var(--muted)" onclick="cancelDeleteConfirm()">取消</button>
</div></div>

<div class="modal-overlay" id="batchOverlay" onclick="closeBatchOverlay(event)">
<div class="modal" onclick="event.stopPropagation()">
<h3 style="color:#c0392b">批量删除</h3>
<div id="batchMenu">
<button class="modal-btn" onclick="showBatchRange()">连续ID删除</button>
<button class="modal-btn danger" onclick="showBatchClear()">全部删除</button>
<button class="modal-btn" style="color:var(--muted)" onclick="closeBatchAll()">取消</button>
</div>
<div id="batchRange" style="display:none">
<p style="color:var(--muted);font-size:var(--fs-sm);margin-bottom:var(--ps)">删除从起始ID到结束ID之间的所有指纹模板</p>
<div class="setting-row"><span class="s-label">起始ID</span><input class="s-input wide" type="number" id="batchStartId" min="0" max="99" value="0"></div>
<div class="setting-row"><span class="s-label">结束ID</span><input class="s-input wide" type="number" id="batchEndId" min="0" max="99" value="9"></div>
<button class="modal-btn danger" onclick="confirmBatchDelete()">确认删除</button>
<button class="modal-btn" style="color:var(--muted)" onclick="backToBatchMenu()">返回</button>
</div>
<div id="batchClear" style="display:none">
<p style="color:var(--muted);font-size:var(--fs-sm);margin:var(--pxs) 0 var(--p)">此操作将清空全部指纹库，不可恢复！</p>
<button class="modal-btn danger" onclick="confirmBatchClear()">确认清空</button>
<button class="modal-btn" style="color:var(--muted)" onclick="backToBatchMenu()">返回</button>
</div>
</div></div>

<div class="modal-overlay" id="batchWaitOverlay">
<div class="modal" style="max-width:280px">
<div class="enroll-icon" style="margin:0 auto var(--p)"><svg viewBox="0 0 80 80"><circle class="enroll-icon-ring waiting" cx="40" cy="40" r="30"/></svg></div>
<p style="color:var(--text);font-size:var(--fs-sm);margin-bottom:var(--pxs)" id="batchWaitMsg">正在处理...</p>
<p style="color:var(--muted);font-size:var(--fs-xs)">请稍候</p>
</div></div>

<div class="modal-overlay enroll-overlay" id="enrollOverlay">
<div class="modal enroll-modal" style="max-width:380px">
<svg class="enroll-icon" id="enrollIcon" viewBox="0 0 80 80">
  <circle class="enroll-icon-ring" id="enrollRing" cx="40" cy="40" r="30"/>
  <text class="enroll-icon-finger" id="enrollFinger" x="40" y="44" text-anchor="middle" font-size="32">&#x1F590;</text>
</svg>
<div class="enroll-msg" id="enrollMsg">准备中...</div>
<div class="enroll-detail" id="enrollDetail"></div>
<div class="enroll-step" id="enrollStep"></div>
<button class="enroll-cancel-btn" id="enrollCancelBtn" onclick="cancelEnroll()">取消录入</button>
</div></div>

<script>
var STATE={}, NOTEPAD={}, SETTINGS={}, ENROLLED=[], currentPage=0, modalId=0;
var FP_TOTAL=100, FP_PER_PAGE=16, FP_PAGES=7;
var enrollPollTimer=null, enrollPrevPhase=-1, cancelTimeout=null;

function $(id){return document.getElementById(id)}
function esc(s){return s.replace(/&/g,'&').replace(/</g,'<').replace(/>/g,'>').replace(/"/g,'"')}

var _pendingAuthCreds=sessionStorage.getItem('zw111_auth');
function showLogin(){
  $('loginOverlay').classList.add('active');
  document.getElementById('loginUser').addEventListener('keydown',function(e){if(e.key==='Enter')doLogin()});
  document.getElementById('loginPass').addEventListener('keydown',function(e){if(e.key==='Enter')doLogin()});
}
function doLogin(){
  var u=document.getElementById('loginUser').value;
  var p=document.getElementById('loginPass').value;
  if(!u||!p){document.getElementById('loginError').textContent='请输入用户名和密码';return}
  var cred=btoa(u+':'+p);
  _pendingAuthCreds=cred;
  fetch('/api/state',{headers:{'X-Auth-Credentials':cred}}).then(function(r){return r.json()}).then(function(d){
    if(d.auth===false){_pendingAuthCreds=null;sessionStorage.removeItem('zw111_auth');document.getElementById('loginError').textContent='用户名或密码错误';return}
    sessionStorage.setItem('zw111_auth',cred);
    location.reload();
  }).catch(function(){_pendingAuthCreds=null;document.getElementById('loginError').textContent='网络错误'})
}
var _api=function(m,p,b){
  var opts={method:m,headers:{}};
  if(_pendingAuthCreds)opts.headers['X-Auth-Credentials']=_pendingAuthCreds;
  if(b){opts.body=JSON.stringify(b);opts.headers['Content-Type']='application/json'}
  return fetch(p,opts).then(function(r){return r.text()}).then(function(t){
    var d;try{d=JSON.parse(t)}catch(e){return t}
    if(d.auth===false){_pendingAuthCreds=null;sessionStorage.removeItem('zw111_auth');showLogin();throw new Error('auth')}
    return d
  })
};

function toast(m,t){var e=$('toast');e.textContent=m;e.className='toast-'+t;e.style.animation='none';e.offsetHeight;e.style.animation='fadeOut 3s forwards'}
function loadState(){return _api('GET','/api/state').then(function(r){if(r)STATE=r})}
function loadSettings(){return _api('GET','/api/settings').then(function(r){if(r){SETTINGS=r;renderSettings()}})}
function loadEnrolled(){return _api('GET','/api/enrolled').then(function(r){if(r&&r.enrolled){ENROLLED=r.enrolled;renderFingerprint()}}).catch(function(){})}

function renderSettings(){
  $('setScoreLevel').value=SETTINGS.score_level||0;
  $('setEnrollMax').value=SETTINGS.enroll_max||5;
  $('setDupBlock').checked=(SETTINGS.dup_block==1);
}

function onSettingChange(key,val){
  var body={};body[key]=parseInt(val)||0;
  _api('POST','/api/settings',body).then(function(r){
    if(r&&r.ok){toast('设置已保存','ok')}
    else toast('保存失败','err')
  }).catch(function(){toast('网络错误','err')})
}

function renderFingerprint(){
  var g=$('fpGrid'),start=currentPage*FP_PER_PAGE,end=Math.min(start+FP_PER_PAGE-1,FP_TOTAL-1),html='';
  for(var id=start;id<=end;id++){
    var n=NOTEPAD[id]||'';
    var enrolledCls=(ENROLLED[id])?' enrolled':'';
    html+='<div class="fp-item" onclick="openModal('+id+')"><div class="fp-avatar'+enrolledCls+'"><svg viewBox="0 0 24 24"><path d="M12 12c2.7 0 4.8-2.1 4.8-4.8S14.7 2.4 12 2.4 7.2 4.5 7.2 7.2 9.3 12 12 12zm0 2.4c-3.2 0-9.6 1.6-9.6 4.8v1.2c0 .66.54 1.2 1.2 1.2h16.8c.66 0 1.2-.54 1.2-1.2v-1.2c0-3.2-6.4-4.8-9.6-4.8z"/></svg></div><div class="fp-id">ID: '+id+'</div><div class="fp-name">'+(n?esc(n):'')+'</div></div>';
  }
  g.innerHTML=html;
  var p=$('pagination'),ph='';
  for(var i=0;i<FP_PAGES;i++)ph+='<button class="'+(i===currentPage?'active':'')+'" onclick="goPage('+i+')">'+(i+1)+'</button>';
  p.innerHTML=ph
}

function openModal(id){
  modalId=id;$('modalTitle').textContent='ID: '+id;
  $('noteEditor').classList.remove('active');$('noteInput').value=NOTEPAD[id]||'';$('noteStatus').textContent='';
  var enrolled=ENROLLED[id];
  $('btnEnroll').style.display=enrolled?'none':'block';
  $('btnDelete').style.display=enrolled?'block':'none';
  $('modalOverlay').classList.add('active')
}
function closeModal(e){if(!e||e.target===$('modalOverlay'))$('modalOverlay').classList.remove('active')}
function modalNote(){$('noteEditor').classList.toggle('active');$('noteInput').value=NOTEPAD[modalId]||'';$('noteInput').focus()}
function modalDelete(){
  if(!ENROLLED[modalId]){toast('该ID未注册指纹','info');closeModal();return}
  $('deleteMsg').textContent='将删除 ID:'+modalId+' 的指纹模板及备注信息，此操作不可恢复。';
  $('deleteOverlay').classList.add('active')
}
function closeDeleteOverlay(e){if(!e||e.target===$('deleteOverlay'))$('deleteOverlay').classList.remove('active')}
function cancelDeleteConfirm(){$('deleteOverlay').classList.remove('active');closeModal()}
function confirmDelete(){
  var id=modalId;
  NOTEPAD[id]='';
  ENROLLED[id]=false;
  renderFingerprint();
  $('deleteOverlay').classList.remove('active');
  closeModal();
  _api('POST','/api/delete',{page:id,count:1}).then(function(r){
    if(r&&r.ok){loadEnrolled();toast('ID:'+id+' 已删除','ok')}else{toast('删除失败','err')}
  }).catch(function(){toast('网络错误','err')})
}
function saveNote(){
  var v=$('noteInput').value,f=v.replace(/[^a-zA-Z0-9]/g,'');
  if(v!==f){$('noteInput').value=f;$('noteStatus').textContent='仅允许字母数字';return}
  $('noteStatus').textContent='保存中...';
  _api('POST','/api/write_notepad',{page:modalId,content:f}).then(function(r){
    if(r&&r.ok){NOTEPAD[modalId]=f;$('noteStatus').textContent='已保存';setTimeout(function(){$('noteEditor').classList.remove('active');renderFingerprint();},800)}
    else $('noteStatus').textContent='保存失败'
  }).catch(function(){$('noteStatus').textContent='网络错误'})
}
function cancelNote(){$('noteEditor').classList.remove('active')}

function goPage(p){if(p>=0&&p<FP_PAGES){currentPage=p;renderFingerprint()}}
function doVerify(){_api('POST','/api/verify',{}).then(function(r){if(r&&r.ok)toast('验证已启动','ok')})}
function doCancel(){_api('POST','/api/cancel',{}).then(function(r){if(r&&r.ok)toast('已取消','ok')})}
function doRefresh(){loadAll().then(function(){renderAll();renderFingerprint();toast('已刷新','ok')})}
function loadAll(){return loadState().then(function(){var ps=[];for(var i=0;i<100;i++)ps.push(loadNotepad(i));return Promise.all(ps)})}
function loadNotepad(pageIdx){return _api('POST','/api/read_notepad',{page:pageIdx}).then(function(r){if(r&&r.content!==undefined)NOTEPAD[pageIdx]=r.content}).catch(function(){})}
function renderAll(){
  var m=$('moduleInfo');
  function fv(k){var v=STATE[k];return(v!==undefined&&v!==null)?v:'N/A'}
  function infoItem(label,val){return'<div class="info-item"><span class="info-label">'+label+'</span><span class="info-value">'+val+'</span></div>'}
  var sensorSize=fv('Sensor_Size');sensorSize=sensorSize.replace(/ px/g,'').replace('px','');
  m.innerHTML=

    infoItem('设备型号',fv('Product_SN'))+
    infoItem('固件版本',fv('Software_Ver'))+
    infoItem('生产厂商',fv('Manufacturer'))+
    infoItem('传感器型号',fv('Sensor_Name'))+
    infoItem('传感器尺寸',sensorSize)+
    infoItem('传感器校准',fv('Sensor_Check_Result'))+
    infoItem('指纹库数量',fv('Database_Size'))+
    infoItem('已注册指纹数',fv('Stored_Count'))+
    infoItem('通讯波特率',fv('Baud_Rate'))+
    infoItem('通讯地址',fv('Device_Address'));
}
function switchTab(t){
  document.querySelectorAll('.tabs button').forEach(function(b,i){b.classList.toggle('active',(t==='fingerprint'&&i===0)||(t==='settings'&&i===1))});
  $('tab-fingerprint').classList.toggle('active',t==='fingerprint');$('tab-settings').classList.toggle('active',t==='settings');
  if(t==='settings'){loadState().then(function(){renderAll();loadSettings()})}else{renderFingerprint()}
}

function setEnrollRingClass(cls){var ring=$('enrollRing'),finger=$('enrollFinger');ring.className='enroll-icon-ring '+(cls||'');finger.className='enroll-icon-finger '+(cls||'');}

function startEnroll(){
  if(!modalId&&modalId!==0){toast('请先选择指纹ID','err');return}
  if(ENROLLED[modalId]){toast('该ID已注册指纹','info');return}
  // 清除上次未完成的取消超时, 防止新弹窗被意外关闭
  if(cancelTimeout){clearTimeout(cancelTimeout);cancelTimeout=null}
  if(enrollPollTimer){clearInterval(enrollPollTimer);enrollPollTimer=null}
  closeModal();
  $('enrollMsg').textContent='准备中...';$('enrollDetail').textContent='';$('enrollStep').textContent='';
  $('enrollCancelBtn').style.display='block';$('enrollCancelBtn').disabled=false;
  $('enrollCancelBtn').textContent='取消录入';$('enrollCancelBtn').onclick=cancelEnroll;
  setEnrollRingClass('');$('enrollOverlay').classList.add('active');enrollPrevPhase=-1;
  _api('POST','/api/enroll',{page:modalId,times:0}).then(function(r){
    if(!r||!r.ok){$('enrollMsg').textContent='启动失败: '+(r?r.msg:'网络错误');setEnrollRingClass('error');return}
    enrollPrevPhase=-1;enrollPollTimer=setInterval(pollEnrollStatus,400);
  }).catch(function(){$('enrollMsg').textContent='网络错误';setEnrollRingClass('error')})
}

function pollEnrollStatus(){
  _api('GET','/api/enroll_status').then(function(s){
    if(!s)return;
    var phase=s.phase;if(phase===enrollPrevPhase)return;enrollPrevPhase=phase;
    $('enrollMsg').textContent=s.message||'';$('enrollDetail').textContent=s.confirm||'';$('enrollStep').textContent='进度: '+s.step+'/'+s.total;
    switch(phase){
      case 0:setEnrollRingClass('');break;case 1:setEnrollRingClass('waiting');break;
      case 2:case 3:case 4:case 5:case 6:case 7:case 8:setEnrollRingClass('active');break;
      case 9:setEnrollRingClass('success');$('enrollMsg').textContent='录入完成, 正在保存...';$('enrollDetail').textContent=s.message||'';$('enrollStep').textContent='';$('enrollCancelBtn').textContent='保存中...';$('enrollCancelBtn').disabled=true;clearInterval(enrollPollTimer);ENROLLED[modalId]=false;renderFingerprint();setTimeout(function(){$('enrollOverlay').classList.remove('active');loadEnrolled();},5000);break;
      case 10:setEnrollRingClass('error');$('enrollCancelBtn').textContent='关闭';clearInterval(enrollPollTimer);break;
      case 11:setEnrollRingClass('');$('enrollMsg').textContent='录入已取消';clearInterval(enrollPollTimer);setTimeout(function(){$('enrollOverlay').classList.remove('active');loadEnrolled().then(function(){renderFingerprint();});},1000);break;
    }
    if(!s.busy&&phase!==9&&phase!==11){clearInterval(enrollPollTimer);if(phase===10){$('enrollCancelBtn').textContent='关闭';$('enrollCancelBtn').onclick=function(){clearInterval(enrollPollTimer);$('enrollOverlay').classList.remove('active')};}}
  }).catch(function(){})
}

function cancelEnroll(){
  $('enrollCancelBtn').disabled=true;$('enrollCancelBtn').textContent='正在取消...';
  _api('POST','/api/cancel',{}).then(function(r){
    if(r&&r.ok){$('enrollMsg').textContent='已发送取消指令';}
    // 5s后强制关闭弹窗(防止取消指令卡死时弹窗无法关闭)
    cancelTimeout=setTimeout(function(){
      cancelTimeout=null;
      if($('enrollOverlay').classList.contains('active')){
        clearInterval(enrollPollTimer);enrollPollTimer=null;
        $('enrollOverlay').classList.remove('active');
        loadEnrolled().then(function(){renderFingerprint();});
      }
    },5000);
  }).catch(function(){
    // API调用失败立即关闭
    if(enrollPollTimer){clearInterval(enrollPollTimer);enrollPollTimer=null}
    $('enrollOverlay').classList.remove('active');
    loadEnrolled().then(function(){renderFingerprint();});
  })
}

function showBatchDelete(){$('batchMenu').style.display='block';$('batchRange').style.display='none';$('batchClear').style.display='none';$('batchOverlay').classList.add('active')}
function showBatchRange(){$('batchMenu').style.display='none';$('batchRange').style.display='block';$('batchClear').style.display='none'}
function showBatchClear(){$('batchMenu').style.display='none';$('batchRange').style.display='none';$('batchClear').style.display='block'}
function backToBatchMenu(){$('batchMenu').style.display='block';$('batchRange').style.display='none';$('batchClear').style.display='none'}
function closeBatchAll(){$('batchOverlay').classList.remove('active')}
function closeBatchOverlay(e){if(!e||e.target===$('batchOverlay'))closeBatchAll()}
function showBatchWait(){$('batchWaitOverlay').classList.add('active')}
function hideBatchWait(){$('batchWaitOverlay').classList.remove('active')}

function confirmBatchDelete(){
  var s=parseInt($('batchStartId').value)||0,e=parseInt($('batchEndId').value)||0;
  if(s>e){var t=s;s=e;e=t}if(s<0)s=0;if(e>99)e=99;
  for(var i=s;i<=e;i++){ENROLLED[i]=false;NOTEPAD[i]='';}
  renderFingerprint();closeBatchAll();showBatchWait();
  $('batchWaitMsg').textContent='正在批量删除 ID '+s+'~'+e+'...';
  var result=null,done=false;
  _api('POST','/api/batch_delete',{start_id:s,end_id:e}).then(function(r){result=r;done=true}).catch(function(){done=true});
  setTimeout(function(){if(!done)setTimeout(function(){finishBatchDel(result,s,e)},2000);else finishBatchDel(result,s,e)},3000);
}

function finishBatchDel(result,s,e){hideBatchWait();if(result&&result.ok)toast(result.msg||'批量删除完成','ok');else toast(result&&result.msg?result.msg:'批量删除失败','err');loadEnrolled().then(function(){renderFingerprint()});loadState().then(function(){renderAll()})}

function confirmBatchClear(){
  for(var i=0;i<100;i++){ENROLLED[i]=false;NOTEPAD[i]='';}
  renderFingerprint();closeBatchAll();showBatchWait();
  $('batchWaitMsg').textContent='正在清空全部指纹库...';
  var result=null,done=false;
  _api('POST','/api/clear',{}).then(function(r){result=r;done=true}).catch(function(){done=true});
  setTimeout(function(){if(!done)setTimeout(function(){finishBatchClear(result)},2000);else finishBatchClear(result)},3000);
}

function finishBatchClear(result){hideBatchWait();if(result&&result.ok)toast('指纹库已清空','ok');else toast('清空失败','err');loadEnrolled().then(function(){renderFingerprint()});loadState().then(function(){renderAll()})}

renderFingerprint();
if(!_pendingAuthCreds)showLogin();
else{loadAll().then(function(){renderAll()});loadEnrolled().then(function(){renderFingerprint()});loadSettings()}
setInterval(function(){loadState().then(renderAll);loadEnrolled().then(function(){renderFingerprint()})},15000);
</script></body></html>
)rawliteral";