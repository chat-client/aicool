"use strict";(()=>{var se=Object.defineProperty;var fe=(t,e,r)=>e in t?se(t,e,{enumerable:!0,configurable:!0,writable:!0,value:r}):t[e]=r;var y=(t,e,r)=>(fe(t,typeof e!="symbol"?e+"":e,r),r);var ne=typeof global=="object"&&global&&global.Object===Object&&global,B=ne;var ie=typeof self=="object"&&self&&self.Object===Object&&self,le=B||ie||Function("return this")(),S=le;var ue=S.Symbol,I=ue;var U=Object.prototype,de=U.hasOwnProperty,pe=U.toString,O=I?I.toStringTag:void 0;function me(t){var e=de.call(t,O),r=t[O];try{t[O]=void 0;var o=!0}catch{}var s=pe.call(t);return o&&(e?t[O]=r:delete t[O]),s}var D=me;var ce=Object.prototype,xe=ce.toString;function he(t){return xe.call(t)}var F=he;var ge="[object Null]",ye="[object Undefined]",V=I?I.toStringTag:void 0;function Ee(t){return t==null?t===void 0?ye:ge:V&&V in Object(t)?D(t):F(t)}var G=Ee;function be(t){return t!=null&&typeof t=="object"}var z=be;var Ce="[object Symbol]";function Ie(t){return typeof t=="symbol"||z(t)&&G(t)==Ce}var K=Ie;var ve=/\s/;function we(t){for(var e=t.length;e--&&ve.test(t.charAt(e)););return e}var q=we;var Re=/^\s+/;function Oe(t){return t&&t.slice(0,q(t)+1).replace(Re,"")}var $=Oe;function Te(t){var e=typeof t;return t!=null&&(e=="object"||e=="function")}var T=Te;var Q=0/0,Se=/^[-+]0x[0-9a-f]+$/i,Ae=/^0b[01]+$/i,ke=/^0o[0-7]+$/i,Le=parseInt;function Me(t){if(typeof t=="number")return t;if(K(t))return Q;if(T(t)){var e=typeof t.valueOf=="function"?t.valueOf():t;t=T(e)?e+"":e}if(typeof t!="string")return t===0?t:+t;t=$(t);var r=Ae.test(t);return r||ke.test(t)?Le(t.slice(2),r?2:8):Se.test(t)?Q:+t}var M=Me;var He=function(){return S.Date.now()},A=He;var Pe="Expected a function",We=Math.max,_e=Math.min;function Ne(t,e,r){var o,s,a,n,f,i,u=0,d=!1,p=!1,m=!0;if(typeof t!="function")throw new TypeError(Pe);e=M(e)||0,T(r)&&(d=!!r.leading,p="maxWait"in r,a=p?We(M(r.maxWait)||0,e):a,m="trailing"in r?!!r.trailing:m);function w(l){var g=o,R=s;return o=s=void 0,u=l,n=t.apply(R,g),n}function _(l){return u=l,f=setTimeout(C,e),d?w(l):n}function c(l){var g=l-i,R=l-u,j=e-g;return p?_e(j,a-R):j}function h(l){var g=l-i,R=l-u;return i===void 0||g>=e||g<0||p&&R>=a}function C(){var l=A();if(h(l))return N(l);f=setTimeout(C,c(l))}function N(l){return f=void 0,m&&o?w(l):(o=s=void 0,n)}function oe(){f!==void 0&&clearTimeout(f),u=0,o=i=s=f=void 0}function ae(){return f===void 0?n:N(A())}function L(){var l=A(),g=h(l);if(o=arguments,s=this,i=l,g){if(f===void 0)return _(i);if(p)return clearTimeout(f),f=setTimeout(C,e),w(i)}return f===void 0&&(f=setTimeout(C,e)),n}return L.cancel=oe,L.flush=ae,L}var H=Ne;var E={MAX_FILE_SIZE:52428800,MAX_CONCURRENT:3,DEBOUNCE_DELAY:300,CONVERSION_QUALITY:.9,RETRY_ATTEMPTS:2,ANIMATION_FPS:24,MAX_CACHE_ITEMS:10},X={HEIC_IMAGES:'img[src*=".HEIC"], img[src*=".heic"], img[src*=".HEIF"], img[src*=".heif"]',PROCESSING_CLASS:"heic-processing",ERROR_CLASS:"heic-error"},b={ORIGINAL_SRC:"data-original-src",PROCESSED:"data-heic-processed",ERROR_COUNT:"data-error-count"},x={FILE_TOO_LARGE:"\u56FE\u7247\u6587\u4EF6\u8FC7\u5927\uFF0C\u8D85\u8FC750MB\u9650\u5236",INVALID_FORMAT:"\u4E0D\u662F\u6709\u6548\u7684HEIC\u683C\u5F0F\u6587\u4EF6",NETWORK_ERROR:"\u7F51\u7EDC\u9519\u8BEF\uFF0C\u65E0\u6CD5\u83B7\u53D6\u56FE\u7247",CONVERSION_FAILED:"HEIC\u8F6C\u6362\u5931\u8D25",CORS_ERROR:"\u8DE8\u57DF\u8BBF\u95EE\u88AB\u62D2\u7EDD"};var v=null;async function Y(){if(v!==null)return v;if(typeof VideoDecoder>"u")return v=!1;try{let t={codec:"hvc1.1.6.L120.90"};return(await VideoDecoder.isConfigSupported({...t,hardwareAcceleration:"prefer-hardware"})).supported?v=!0:v=(await VideoDecoder.isConfigSupported(t)).supported??!1}catch{return v=!1}}var P="/webcool/html/js/heic-worker-hard.js";var W="/webcool/html/js/heic-worker-soft.js";var je=new Set(["mif1","heic","heix"]),Z=new Set(["msf1","hevc","hevx"]);function J(t){if(t.byteLength<12)return"";let e=new Uint8Array(t,8,4);return String.fromCharCode(...Array.from(e)).replace(/\0/g," ").trim()}function Be(t){let e=J(t);return je.has(e)||Z.has(e)}function Ue(t){return Z.has(J(t))}var k=class{constructor(){y(this,"processedImages",new WeakSet);y(this,"processingQueue",new Map);y(this,"urlCache",new Map);y(this,"animationTimers",new Map);y(this,"workerPromise");y(this,"workerTaskId",0);y(this,"workerCallbacks",new Map);this.workerPromise=this.initWorker()}async initWorker(){let e=await Y();console.log("supported:"+e+", hardWorkerUrl:"+P+", softWorkerUrl:"+W);let r=new Worker(e?P:W,{type:"module"});return r.onmessage=o=>{let{id:s,success:a,error:n,type:f,url:i,frames:u,width:d,height:p}=o.data,m=this.workerCallbacks.get(s);m&&(a?m.resolve({type:f,url:i,frames:u,width:d,height:p}):m.reject(new Error(n)),this.workerCallbacks.delete(s))},r}getCacheEntry(e){if(!this.urlCache.has(e))return;let r=this.urlCache.get(e);return this.urlCache.delete(e),this.urlCache.set(e,r),r}setCacheEntry(e,r){if(this.urlCache.has(e))this.urlCache.delete(e);else for(;this.urlCache.size>=E.MAX_CACHE_ITEMS;){let o=this.urlCache.keys().next().value;if(o){let s=this.urlCache.get(o);s&&(s.type==="single"?URL.revokeObjectURL(s.url):s.frames.forEach(a=>URL.revokeObjectURL(a.url))),this.urlCache.delete(o),console.debug(`LRU Cache: evicted ${o}`)}else break}this.urlCache.set(e,r)}isImageProcessed(e){return this.processedImages.has(e)||e.hasAttribute(b.PROCESSED)}markImageAsProcessed(e){this.processedImages.add(e),e.setAttribute(b.PROCESSED,"true")}resetImageProcessed(e){this.processedImages.delete(e),this.animationTimers.has(e)&&(clearInterval(this.animationTimers.get(e)),this.animationTimers.delete(e)),e.removeAttribute(b.PROCESSED),e.removeAttribute(b.ORIGINAL_SRC),e.classList.remove("heic-processing","heic-converted")}async fetchImageData(e){let r;try{r=await fetch(e)}catch(n){if(n.name==="TypeError"){let f=!1;try{f=new URL(e).origin!==location.origin}catch{}throw new Error(f?x.CORS_ERROR:x.NETWORK_ERROR)}throw new Error(x.NETWORK_ERROR)}if(!r.ok)throw new Error(`HTTP ${r.status}: ${r.statusText}`);let o=await r.blob();if(o.size>E.MAX_FILE_SIZE)throw new Error(x.FILE_TOO_LARGE);let s=await o.arrayBuffer(),a=o.type==="image/heic"||o.type==="image/heif"||o.type==="image/heic-sequence"||o.type==="image/heif-sequence";if(!Be(s)&&!a)throw new Error(x.INVALID_FORMAT);return s}setupUrlAnimation(e,r){let o=0,s=()=>{e.src=r[o].url,o=(o+1)%r.length};s();let a=r.reduce((f,i)=>f+i.durationMs,0)/r.length,n=setInterval(s,Math.max(16,Math.round(a)));this.animationTimers.set(e,n),this.markImageAsProcessed(e)}async convertImage(e,r={}){if(this.isImageProcessed(e))return{success:!0};if(this.processingQueue.has(e))return this.processingQueue.get(e);let o=this._doConvert(e,r);this.processingQueue.set(e,o);try{return await o}finally{this.processingQueue.delete(e)}}async _doConvert(e,r={}){let o=e.src,s=/(https|http).*?\.(heic|heif)/gi,a=o.match(s)?.[0]||o,{maxRetries:n=E.RETRY_ATTEMPTS}=r;e.hasAttribute(b.ORIGINAL_SRC)||e.setAttribute(b.ORIGINAL_SRC,o);let f=this.getCacheEntry(a);if(console.debug("get cacheKey:"+a+", cachedEntry:",f),f)return f.type==="single"?(e.src=f.url,e.classList.add("heic-converted"),this.markImageAsProcessed(e)):f.type==="animated"&&(this.setupUrlAnimation(e,f.frames),e.classList.add("heic-converted")),{success:!0};e.classList.add("heic-processing");let i;for(let u=0;u<n;u++)try{let d=await this.fetchImageData(o),p=Ue(d)?"animated":"single",m=this.workerTaskId++,w=await this.workerPromise,c=await new Promise((h,C)=>{this.workerCallbacks.set(m,{resolve:h,reject:C}),w.postMessage({id:m,buffer:d,options:r,type:p},[d])});if(c.success===!1)throw new Error(c.error||"Worker error");if(c.type==="animated")return this.setCacheEntry(a,{type:"animated",frames:c.frames}),this.setupUrlAnimation(e,c.frames),e.classList.remove("heic-processing"),e.classList.add("heic-converted"),{success:!0};if(c.type==="single"){let h=c.url;return this.setCacheEntry(a,{type:"single",url:h}),console.debug("set cacheKey:"+a+", objectURL:"+h),e.src=h,e.classList.remove("heic-processing"),e.classList.add("heic-converted"),this.markImageAsProcessed(e),{success:!0}}}catch(d){i=d,console.warn(`HEIC\u8F6C\u6362\u5C1D\u8BD5 ${u+1}/${n} \u5931\u8D25:`,d.message),u<n-1&&await new Promise(p=>setTimeout(p,1e3*(u+1)))}return this.handleConversionError(e,i,o)}async convertAllImages(e,r={}){let o=[],s=Array.from(e);for(let a=0;a<s.length;a+=E.MAX_CONCURRENT){let n=s.slice(a,a+E.MAX_CONCURRENT);(await Promise.allSettled(n.map(i=>this.convertImage(i,r)))).forEach(i=>{i.status==="fulfilled"?o.push(i.value):o.push({success:!1,error:{type:"unknown",message:i.reason?.message??"Promise rejected"}})})}return o}cleanup(){this.urlCache.forEach(e=>{e.type==="single"?URL.revokeObjectURL(e.url):e.frames.forEach(r=>URL.revokeObjectURL(r.url))}),this.urlCache.clear(),this.processingQueue.clear(),this.animationTimers.forEach(e=>clearInterval(e)),this.animationTimers.clear(),this.workerPromise.then(e=>e.terminate())}handleConversionError(e,r,o){e.classList.remove("heic-processing"),e.classList.add("heic-converted");let s="unknown",a=r?.message??x.CONVERSION_FAILED,n=a;return a.includes("CORS")||a.includes("cross-origin")||a.includes("Access-Control-Allow-Origin")?(s="cors",a=x.CORS_ERROR,n="\u8DE8\u57DF\u8BBF\u95EE\u88AB\u62D2\u7EDD"):a.includes("Failed to fetch")||a.includes("\u7F51\u7EDC")?(s="network",a=x.NETWORK_ERROR,n="\u7F51\u7EDC\u8BF7\u6C42\u5931\u8D25"):a.includes("50MB")?(s="size",n="\u6587\u4EF6\u8FC7\u5927"):a.includes("\u683C\u5F0F")||a.includes("HEIC")?(s="format",n="\u683C\u5F0F\u4E0D\u652F\u6301"):a.includes("\u8F6C\u6362")?(s="conversion",n="\u8F6C\u6362\u5931\u8D25"):r?.name==="AbortError"&&(s="network",n="\u8BF7\u6C42\u8D85\u65F6"),e.onclick=f=>{f.preventDefault(),s==="cors"?confirm(`\u56FE\u7247\u56E0\u8DE8\u57DF\u9650\u5236\u65E0\u6CD5\u8F6C\u6362\u3002

\u9519\u8BEF\u8BE6\u60C5: ${n}

\u662F\u5426\u5728\u65B0\u7A97\u53E3\u4E2D\u67E5\u770B\u539F\u56FE\uFF1F`)&&window.open(o,"_blank"):window.open(o,"_blank")},console.warn("\u{1F534} HEIC\u8F6C\u6362\u5931\u8D25:",{src:o,type:s,message:a,originalError:r}),{success:!1,error:{type:s,message:a,originalError:r}}}};function ee(){console.log("\u{1F5BC}\uFE0F View HEIC Loaded"),De();let t=new k;te(t);let e=Fe(t),r=()=>{e.disconnect(),t.cleanup()};return window.addEventListener("beforeunload",r,{once:!0}),{converter:t,stop:r}}function De(){let t="view-heic-styles";if(document.getElementById(t))return;let e=document.createElement("style");e.id=t,e.textContent=`
    /* HEIC\u56FE\u7247\u5904\u7406\u72B6\u6001\u6837\u5F0F */
    .heic-processing {
      position: relative;
      opacity: 0.7;
      transition: opacity 0.3s ease;
    }

    .heic-processing::after {
      content: "\u{1F504} \u8F6C\u6362\u4E2D...";
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: rgba(0, 0, 0, 0.8);
      color: white;
      padding: 4px 8px;
      border-radius: 4px;
      font-size: 12px;
      white-space: nowrap;
      z-index: 1000;
      pointer-events: none;
    }

    .heic-converted {
      opacity: 1;
      transition: opacity 0.3s ease;
    }

    .heic-error {
      position: relative;
      border: 2px dashed #ff6b6b !important;
      opacity: 0.8;
      filter: grayscale(50%);
      cursor: pointer;
    }

    .heic-error::after {
      content: "\u274C \u8F6C\u6362\u5931\u8D25 - \u70B9\u51FB\u67E5\u770B\u539F\u56FE";
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: rgba(255, 107, 107, 0.9);
      color: white;
      padding: 4px 8px;
      border-radius: 4px;
      font-size: 12px;
      white-space: nowrap;
      z-index: 1000;
      pointer-events: none;
    }

    .heic-error:hover {
      opacity: 1;
      filter: grayscale(0%);
    }
  `,document.head.appendChild(e)}async function te(t){let e=document.querySelectorAll(X.HEIC_IMAGES);if(e.length===0)return;console.log(`\u{1F4F7} \u53D1\u73B0 ${e.length} \u5F20HEIC\u56FE\u7247\uFF0C\u5F00\u59CB\u8F6C\u6362...`);let r=await t.convertAllImages(e),o=r.filter(a=>a.success).length,s=r.length-o;console.log(`\u2705 \u8F6C\u6362\u5B8C\u6210: ${o} \u6210\u529F, ${s} \u5931\u8D25`),s>0&&console.warn("\u26A0\uFE0F \u90E8\u5206\u56FE\u7247\u8F6C\u6362\u5931\u8D25\uFF0C\u53EF\u80FD\u662F\u7531\u4E8ECORS\u9650\u5236\u6216\u683C\u5F0F\u95EE\u9898")}function Fe(t){let e=H(()=>{te(t)},E.DEBOUNCE_DELAY,{trailing:!0,leading:!1}),r=new MutationObserver(o=>{let s=!1;for(let a of o){if(a.type==="attributes"&&a.attributeName==="src"){let n=a.target;if(n.nodeType===Node.ELEMENT_NODE&&n.tagName==="IMG"){let f=n;console.log("\u{1F504} \u68C0\u6D4B\u5230img src\u53D8\u5316:",f.src),f.src.startsWith("blob:")||(t.resetImageProcessed(f),Ve(f)&&(console.log("\u{1F504} \u68C0\u6D4B\u5230img src\u53D8\u5316\u4E3AHEIC\u56FE\u7247\uFF0C\u91CD\u7F6E\u5904\u7406\u72B6\u6001:",f.src),s=!0))}}if(s)break}s&&(console.log("\u{1F504} \u68C0\u6D4B\u5230\u65B0\u7684HEIC\u56FE\u7247\uFF0C\u51C6\u5907\u5904\u7406..."),e())});return r.observe(document.body,{childList:!0,subtree:!0,attributes:!0,attributeFilter:["src"]}),r}function Ve(t){let e=t.src.toLowerCase(),r=t.alt.toLowerCase();return e.endsWith(".heic")||e.endsWith(".heif")||r.endsWith(".heic")||r.endsWith(".heif")}function re(){let t=ee();return window.ViewHEIC=t,t}document.readyState==="loading"?document.addEventListener("DOMContentLoaded",()=>void re(),{once:!0}):re();})();
/*! Bundled license information:

lodash-es/lodash.js:
  (**
   * @license
   * Lodash (Custom Build) <https://lodash.com/>
   * Build: `lodash modularize exports="es" --repo lodash/lodash#4.18.1 -o ./`
   * Copyright OpenJS Foundation and other contributors <https://openjsf.org/>
   * Released under MIT license <https://lodash.com/license>
   * Based on Underscore.js 1.8.3 <http://underscorejs.org/LICENSE>
   * Copyright Jeremy Ashkenas, DocumentCloud and Investigative Reporters & Editors
   *)
*/
//# sourceMappingURL=view-heic.js.map
