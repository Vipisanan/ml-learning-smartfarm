(function () {
  "use strict";

  // ---- config ----
  var LIVE_MS = 10000;      // /api/data poll
  var HIST_MS = 300000;     // /api/history refresh (5 min)

  // ---- i18n (offline, inline) ----
  var I18N = {
    en: {
      offline:"offline", online:"online",
      soil:"soil moisture", temp:"soil temp", raw:"raw",
      last24:"last 24 hours", soil_s:"moisture", temp_s:"temp",
      collecting:"Collecting data…", waiting:"waiting…",
      WATER_NOW:"Water now", WATER_SOON:"Water soon", OK:"Looks good", HOLD:"Hold watering",
      OPTIMAL:"optimal band", CAUTION:"caution band", STRESS:"stress band", WET:"wet band",
      updated:"updated", ago:"ago", refresh:"refreshes every 10s",
      cropLabel:"Crop", cropSaved:"Saved — decisions now use this crop",
      srcModel:"model", srcRule:"fail-safe rule",
      crop_tomato:"Tomato", crop_chili:"Chili", crop_okra:"Okra"
    },
    ta: {
      offline:"ஆஃப்லைன்", online:"ஆன்லைன்",
      soil:"மண் ஈரப்பதம்", temp:"மண் வெப்பம்", raw:"raw",
      last24:"கடந்த 24 மணி", soil_s:"ஈரப்பதம்", temp_s:"வெப்பம்",
      collecting:"தரவு சேகரிக்கிறது…", waiting:"காத்திருக்கிறது…",
      WATER_NOW:"இப்போது நீர்", WATER_SOON:"விரைவில் நீர்", OK:"நன்றாக உள்ளது", HOLD:"நீர் நிறுத்து",
      OPTIMAL:"உகந்த நிலை", CAUTION:"எச்சரிக்கை நிலை", STRESS:"அழுத்த நிலை", WET:"ஈர நிலை",
      updated:"புதுப்பிக்கப்பட்டது", ago:"முன்", refresh:"10 வினாடிக்கு ஒருமுறை",
      cropLabel:"பயிர்", cropSaved:"சேமிக்கப்பட்டது — முடிவுகள் இப்போது இந்த பயிருக்கு",
      srcModel:"மாடல்", srcRule:"fail-safe rule",
      crop_tomato:"தக்காளி", crop_chili:"மிளகாய்", crop_okra:"வெண்டைக்காய்"
    }
  };
  var lang = "en";
  function t(k){ return (I18N[lang] && I18N[lang][k]) || (I18N.en[k] || k); }

  function applyStaticI18n(){
    document.querySelectorAll("[data-i18n]").forEach(function(el){
      el.textContent = t(el.getAttribute("data-i18n"));
    });
  }

  // ---- helpers ----
  function $(id){ return document.getElementById(id); }
  function get(url){
    return fetch(url,{cache:"no-store"}).then(function(r){
      if(!r.ok) throw new Error(url+" "+r.status);
      return r.json();
    });
  }

  var REC_CLASS = { WATER_NOW:"rec-stress", WATER_SOON:"rec-caution", OK:"rec-ok", HOLD:"rec-wet" };
  var BAND_KEY  = { STRESS:"STRESS", CAUTION:"CAUTION", OPTIMAL:"OPTIMAL", WET:"WET" };

  var last = null; // last /api/data payload (for re-render on lang switch)

  function renderData(d){
    last = d;
    $("soil").textContent    = d.soil;
    $("temp").textContent    = (typeof d.tempC==="number") ? d.tempC.toFixed(1) : d.tempC;
    $("soilRaw").textContent = d.soilRaw;

    // recommendation
    $("recVerdict").textContent = t(d.recommendation) || d.recommendation;
    $("recReason").textContent  = d.reason || "";
    var chip = $("bandChip");
    chip.textContent = t(BAND_KEY[d.band] || d.band);
    var card = $("recCard");
    card.className = "rec " + (REC_CLASS[d.recommendation] || "rec-ok");

    // source badge: which decided this (on-device model vs the fail-safe rule)
    var badge = $("srcBadge");
    if (d.source === "model_onboard"){
      badge.textContent = t("srcModel") + (d.confidence >= 0 ? " "+Math.round(d.confidence*100)+"%" : "");
      badge.className = "src-badge src-model";
    } else {
      badge.textContent = t("srcRule");
      badge.className = "src-badge src-rule";
    }

    // online pill (we got a response → online)
    var p = $("statusPill");
    p.className = "pill pill-on";
    $("statusText").textContent = t("online");

    stampUpdated();
  }

  var lastTs = 0;
  function stampUpdated(){ lastTs = Date.now(); tick(); }
  function tick(){
    if(!lastTs){ return; }
    var s = Math.round((Date.now()-lastTs)/1000);
    $("updated").textContent = t("updated")+" "+s+"s "+t("ago")+" · "+t("refresh");
  }
  setInterval(tick, 1000);

  function markOffline(){
    var p = $("statusPill");
    p.className = "pill pill-off";
    $("statusText").textContent = t("offline");
  }

  function renderInfo(i){
    $("deviceName").textContent = i.device || "VP Farm";
    $("fwLine").textContent =
      "fw "+i.fw+" · build "+i.build+" · "+i.ip+" · "+i.ssid+
      " · "+ (i.clients!=null ? i.clients+" client(s)" : "");
  }

  // ---- chart (hand-drawn, no lib) ----
  var histPoints = [];
  function drawChart(){
    var cv = $("chart");
    var empty = $("chartEmpty");
    if(!histPoints.length){ cv.style.display="none"; empty.style.display="block"; empty.textContent=t("collecting"); return; }
    cv.style.display="block"; empty.style.display="none";

    var dpr = window.devicePixelRatio || 1;
    var w = cv.clientWidth, h = 150;
    cv.width = w*dpr; cv.height = h*dpr;
    var x = cv.getContext("2d"); x.scale(dpr,dpr);
    x.clearRect(0,0,w,h);

    var css = getComputedStyle(document.documentElement);
    var accent  = css.getPropertyValue("--accent").trim() || "#3dba6f";
    var caution = css.getPropertyValue("--caution").trim() || "#e0a437";
    var border  = css.getPropertyValue("--border").trim() || "rgba(255,255,255,.08)";

    // grid
    x.strokeStyle = border; x.lineWidth = .5;
    for(var g=0; g<4; g++){ var gy = 8 + g*((h-20)/3); x.beginPath(); x.moveTo(0,gy); x.lineTo(w,gy); x.stroke(); }

    var m = histPoints.map(function(p){return p.m;});
    var c = histPoints.map(function(p){return p.c;});
    line(x, m, 0, 100, accent, w, h);
    var cMin = Math.min.apply(null,c)-1, cMax = Math.max.apply(null,c)+1;
    if(cMax-cMin < 2){ cMax=cMin+2; }
    line(x, c, cMin, cMax, caution, w, h);
  }
  function line(x, arr, min, max, color, w, h){
    if(arr.length<2){ return; }
    x.beginPath();
    for(var i=0;i<arr.length;i++){
      var px = (i/(arr.length-1))*(w-6)+3;
      var py = h-10-((arr[i]-min)/(max-min))*(h-24);
      i ? x.lineTo(px,py) : x.moveTo(px,py);
    }
    x.strokeStyle=color; x.lineWidth=2; x.lineJoin="round"; x.stroke();
  }

  // ---- polling ----
  function pollData(){
    get("/api/data").then(renderData).catch(markOffline);
  }
  function pollHistory(){
    get("/api/history").then(function(d){
      histPoints = (d && d.points) ? d.points : [];
      var pc = document.getElementById("pointCount");
      if(pc){
        var n = (d && d.count!=null) ? d.count : histPoints.length;
        var cap = (d && d.capacity!=null) ? d.capacity : 288;
        pc.textContent = n + " / " + cap;
      }
      drawChart();
    }).catch(function(){ /* keep old chart */ });
  }
  function loadInfo(){
    get("/api/info").then(renderInfo).catch(function(){});
  }

  // ---- crop config ----
  function cropLabel(crop){ return t("crop_"+crop) || crop; }

  function loadCropConfig(){
    get("/api/config").then(function(cfg){
      var sel = $("cropSelect");
      sel.innerHTML = "";
      (cfg.known_crops || []).forEach(function(c){
        var opt = document.createElement("option");
        opt.value = c;
        opt.textContent = cropLabel(c);
        if (c === cfg.crop) opt.selected = true;
        sel.appendChild(opt);
      });
    }).catch(function(){});
  }

  function saveCrop(crop){
    var note = $("cropSavedNote");
    fetch("/api/config", {
      method: "POST",
      headers: {"Content-Type":"application/json"},
      body: JSON.stringify({crop: crop})
    }).then(function(r){
      if(!r.ok) throw new Error("save failed");
      return r.json();
    }).then(function(){
      note.textContent = t("cropSaved");
      note.className = "crop-note saved";
      setTimeout(function(){ note.textContent = ""; note.className = "crop-note"; }, 3000);
    }).catch(function(){
      note.textContent = "";
    });
  }

  $("cropSelect").addEventListener("change", function(){
    saveCrop(this.value);
  });

  // ---- lang toggle ----
  $("langBtn").addEventListener("click", function(){
    lang = (lang==="en") ? "ta" : "en";
    this.textContent = (lang==="en") ? "தமிழ்" : "English";
    document.documentElement.lang = lang;
    applyStaticI18n();
    if(last){ renderData(last); }
    drawChart();
    loadCropConfig();   // re-render option labels in the new language
  });

  window.addEventListener("resize", drawChart);

  // ---- init ----
  applyStaticI18n();
  loadInfo();
  loadCropConfig();
  pollData();   setInterval(pollData, LIVE_MS);
  pollHistory(); setInterval(pollHistory, HIST_MS);
})();
