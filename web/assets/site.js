/* TVE feature guide: draws each page from features.js. No dependencies, works from file://. */
(function () {
  "use strict";
  var D = window.TVE, ICONS = window.TVE_ICONS || {};
  var root = document.documentElement;
  var page = document.body.getAttribute("data-page");

  // ---------- helpers ----------
  function $(sel, el) { return (el || document).querySelector(sel); }
  function $$(sel, el) { return Array.prototype.slice.call((el || document).querySelectorAll(sel)); }
  function esc(s) { return String(s).replace(/[&<>"']/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]; }); }
  function icon(name, cls) {
    return '<svg class="icon' + (cls ? " " + cls : "") + '" viewBox="0 0 24 24" aria-hidden="true">' + (ICONS[name] || "") + "</svg>";
  }
  function byId(list, id) { for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i]; return null; }
  function areaOf(f) { return byId(D.areas, f.area); }
  function chapterOf(f) { return byId(D.chapters, f.chapter); }
  function inArea(id) { return D.features.filter(function (f) { return f.area === id; }); }
  function inChapter(id) { return D.features.filter(function (f) { return f.chapter === id; }); }

  function theme() {
    var t = root.getAttribute("data-theme");
    if (t) return t;
    return window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
  }
  function shot(name, size, t) { return "images/" + size + "/" + name + "-" + (t || theme()) + ".webp"; }
  function featureUrl(f) { return "feature.html#" + f.id; }
  function focusOf(f) { return f.focus || areaOf(f).focus; }
  // The outline on a feature's screenshot: its own focus box when it has one, else the panel it
  // lives in (the map's region). Top-bar dialogs and Home screens are the whole picture: no outline.
  function markOf(f) {
    var a = areaOf(f);
    if (f.focus) return f.focus[2] < 100 ? f.focus : null;
    return a.id !== "topbar" && a.box ? (a.mark || a.box) : null;
  }

  // A path step written with ▾ or ⌄ is a menu button: show the label with the drawn chevron.
  function stepHtml(p) {
    var menu = /[▾⌄]/.test(p);
    var label = esc(p.replace(/\s*[▾⌄]\s*/g, " ").trim());
    return menu ? label + icon("chevron-down", "menu-mark") : label;
  }
  function pathHtml(parts) {
    return '<span class="path">' + parts.map(function (p, i) {
      return (i ? icon("chevron-right") : "") + "<span>" + stepHtml(p) + "</span>";
    }).join("") + "</span>";
  }
  function badgeHtml(f) {
    if (f.privacy === "mac") return '<span class="badge"><i></i>On this Mac</span>';
    if (f.privacy === "key") return '<span class="badge key"><i></i>Your own key</span>';
    return "";
  }
  function cardHtml(f) {
    return '<a class="card" href="' + featureUrl(f) + '">' +
      '<span class="thumb"><img data-shot="' + f.id + '" data-size="focus" src="' + shot(f.id, "focus") + '" alt="" loading="lazy" decoding="async" width="800" height="450"></span>' +
      '<span class="body"><h3>' + esc(f.title) + "</h3>" + pathHtml(f.where) + "<p>" + esc(f.summary) + "</p>" + badgeHtml(f) + "</span></a>";
  }
  function refreshShots() {
    $$("img[data-shot]").forEach(function (img) {
      if (img.hasAttribute("data-fixed")) return;
      img.src = shot(img.getAttribute("data-shot"), img.getAttribute("data-size"));
    });
  }

  // ---------- chrome: icons, theme toggle, header search ----------
  $$("[data-icon]").forEach(function (el) { el.insertAdjacentHTML("afterbegin", icon(el.getAttribute("data-icon"))); });

  var toggle = $("#theme-toggle");
  function paintToggle() {
    if (!toggle) return;
    var t = theme();
    toggle.innerHTML = icon(t === "dark" ? "sun" : "moon");
    toggle.setAttribute("aria-label", t === "dark" ? "Switch to the light theme" : "Switch to the dark theme");
    toggle.title = toggle.getAttribute("aria-label");
  }
  if (toggle) {
    paintToggle();
    toggle.addEventListener("click", function () {
      var next = theme() === "dark" ? "light" : "dark";
      root.setAttribute("data-theme", next);
      try { localStorage.setItem("tve-guide-theme", next); } catch (e) { /* not saved; still switches */ }
      paintToggle();
      refreshShots();
      document.dispatchEvent(new CustomEvent("themechange"));
    });
  }
  if (window.matchMedia) {
    var mq = window.matchMedia("(prefers-color-scheme: light)");
    var onSystem = function () { if (!root.getAttribute("data-theme")) { paintToggle(); refreshShots(); } };
    if (mq.addEventListener) mq.addEventListener("change", onSystem);
  }
  var count = $("[data-feature-count]");
  if (count) count.textContent = D.features.length;

  // ---------- map (home) ----------
  function renderMap() {
    var map = $("#map"), legend = $("#legend"), panel = $("#area-panel");
    var regions = {}, pins = {}, buttons = {};

    D.areas.forEach(function (a) {
      if (a.box) {
        var r = document.createElement("span");
        r.className = "region";
        r.style.left = a.box[0] + "%"; r.style.top = a.box[1] + "%";
        r.style.width = a.box[2] + "%"; r.style.height = a.box[3] + "%";
        map.appendChild(r);
        regions[a.id] = r;
      }
    });
    D.areas.forEach(function (a) {
      if (!a.box) return;
      var p = document.createElement("button");
      p.type = "button"; p.className = "pin"; p.textContent = a.n;
      var at = a.pin || [a.box[0] + a.box[2] / 2, a.box[1] + a.box[3] / 2];
      p.style.left = at[0] + "%";
      p.style.top = "max(20px, " + at[1] + "%)"; // a thin bar's pin stays whole
      p.setAttribute("aria-label", a.name + ": " + inArea(a.id).length + " features");
      p.addEventListener("click", function () { choose(a.id, true); });
      p.addEventListener("mouseenter", function () { hover(a.id, true); });
      p.addEventListener("mouseleave", function () { hover(a.id, false); });
      map.appendChild(p);
      pins[a.id] = p;
    });

    var html = "", sepDone = false;
    D.areas.forEach(function (a) {
      if (!a.box && !sepDone) { html += '<li class="sep" aria-hidden="true">Outside the editor window</li>'; sepDone = true; }
      html += '<li><button type="button" aria-pressed="false" data-area="' + a.id + '">' +
        '<span class="num' + (a.box ? "" : " off") + '">' + a.n + "</span>" +
        '<span class="name">' + esc(a.name) + '<span class="blurb">' + esc(a.blurb) + "</span></span>" +
        '<span class="count">' + inArea(a.id).length + "</span></button></li>";
    });
    $("ol", legend).innerHTML = html;
    $$("button[data-area]", legend).forEach(function (b) {
      var id = b.getAttribute("data-area");
      buttons[id] = b;
      b.addEventListener("click", function () { choose(id, true); });
      b.addEventListener("mouseenter", function () { hover(id, true); });
      b.addEventListener("mouseleave", function () { hover(id, false); });
      b.addEventListener("focus", function () { hover(id, true); });
      b.addEventListener("blur", function () { hover(id, false); });
    });

    function hover(id, on) { if (regions[id]) regions[id].classList.toggle("is-hover", on); }

    function choose(id, fromUser) {
      var a = byId(D.areas, id) || byId(D.areas, "library");
      Object.keys(regions).forEach(function (k) { regions[k].classList.toggle("is-on", k === a.id); });
      Object.keys(pins).forEach(function (k) { pins[k].classList.toggle("is-on", k === a.id); });
      Object.keys(buttons).forEach(function (k) { buttons[k].setAttribute("aria-pressed", String(k === a.id)); });
      var list = inArea(a.id);
      panel.innerHTML =
        '<div class="area-head"><h2 id="area-title">' + a.n + ". " + esc(a.name) + '</h2>' +
        '<span class="count-note">' + list.length + (list.length === 1 ? " feature" : " features") + "</span>" +
        '<a class="all" href="features.html">Every feature by topic</a></div>' +
        '<p class="area-blurb">' + esc(a.blurb) + "</p>" +
        '<div class="cards">' + list.map(cardHtml).join("") + "</div>";
      $(".count-note", panel).style.color = "var(--text-2)";
      if (fromUser) {
        try { history.replaceState(null, "", "#" + a.id); } catch (e) { /* some file:// pages refuse; the area still shows */ }
        if (!a.box) panel.scrollIntoView({ behavior: "smooth", block: "start" });
      }
    }
    choose((location.hash || "").slice(1) || "library", false);
    window.addEventListener("hashchange", function () { choose(location.hash.slice(1), false); });
    document.addEventListener("themechange", function () {
      $("#map-image").src = "images/map-editor-" + theme() + ".webp";
    });
    $("#map-image").src = "images/map-editor-" + theme() + ".webp";
  }

  // ---------- every feature ----------
  function renderAll() {
    var list = $("#feature-list"), toc = $("#toc"), input = $("#filter-q"), chips = $("#area-chips"), note = $("#result-count");
    var params = new URLSearchParams(location.search);
    var state = { q: params.get("q") || "", area: params.get("area") || "" };
    input.value = state.q;
    $$("input[name=q]").forEach(function (i) { if (i !== input) i.value = state.q; });

    chips.innerHTML = '<button type="button" class="chip" data-area="">All areas</button>' + D.areas.map(function (a) {
      return '<button type="button" class="chip" data-area="' + a.id + '">' + esc(a.name) + "</button>";
    }).join("");

    function matches(f, words) {
      var hay = (f.title + " " + f.summary + " " + f.where.join(" ") + " " + f.steps.join(" ") + " " + (f.note || "") + " " + (f.keys || []).join(" ")).toLowerCase();
      return words.every(function (w) { return hay.indexOf(w) >= 0; });
    }
    function draw() {
      var words = state.q.toLowerCase().split(/\s+/).filter(Boolean), shown = 0, html = "", tocHtml = "<h2>Topics</h2>";
      D.chapters.forEach(function (c) {
        var items = inChapter(c.id).filter(function (f) { return (!state.area || f.area === state.area) && matches(f, words); });
        if (!items.length) return;
        shown += items.length;
        tocHtml += '<a href="#' + c.id + '"><span class="dot k-' + c.kind + '"></span>' + esc(c.name) + "<em>" + items.length + "</em></a>";
        html += '<section class="chapter" id="' + c.id + '"><h2><span class="dot k-' + c.kind + '"></span>' + esc(c.name) + "</h2>" +
          items.map(function (f) {
            return '<a class="row" href="' + featureUrl(f) + '"><span class="thumb"><img data-shot="' + f.id + '" data-size="focus" src="' + shot(f.id, "focus") + '" alt="" loading="lazy" decoding="async" width="800" height="450"></span>' +
              '<span class="text"><h3>' + esc(f.title) + "</h3>" + pathHtml(f.where) + "<p>" + esc(f.summary) + "</p>" + badgeHtml(f) + "</span></a>";
          }).join("") + "</section>";
      });
      list.innerHTML = html || '<div class="empty"><strong>No feature matches “' + esc(state.q) + '”.</strong>Try a shorter word, such as “colour”, “caption” or “export”, or choose All areas.</div>';
      toc.innerHTML = tocHtml;
      var filtered = state.q || state.area;
      note.textContent = filtered ? shown + (shown === 1 ? " feature matches" : " features match") : D.features.length + " features in " + D.chapters.length + " topics";
      $$(".chip", chips).forEach(function (b) { b.setAttribute("aria-pressed", String(b.getAttribute("data-area") === state.area)); });
    }
    function sync() {
      var p = new URLSearchParams();
      if (state.q) p.set("q", state.q);
      if (state.area) p.set("area", state.area);
      var qs = p.toString();
      try { history.replaceState(null, "", location.pathname + (qs ? "?" + qs : "")); } catch (e) { /* file:// may refuse; filtering still works */ }
    }
    input.addEventListener("input", function () { state.q = input.value.trim(); draw(); sync(); });
    chips.addEventListener("click", function (e) {
      var b = e.target.closest(".chip"); if (!b) return;
      state.area = b.getAttribute("data-area"); draw(); sync();
    });
    var head = $(".site-head input[name=q]");
    if (head) head.form.addEventListener("submit", function (e) { e.preventDefault(); input.value = head.value; state.q = head.value.trim(); draw(); sync(); input.focus(); });
    draw();
  }

  // ---------- one feature ----------
  function renderFeature() {
    var out = $("#feature");
    function draw() {
      var id = (location.hash || "").slice(1);
      var f = byId(D.features, id);
      if (!f) {
        out.innerHTML = '<div class="empty" style="padding-top:64px"><strong>That feature is not in the guide.</strong>' +
          'It may have been renamed. <a href="features.html">See every feature</a> or <a href="index.html">find it on the map</a>.</div>';
        document.title = "Feature not found · TVE feature guide";
        return;
      }
      var a = areaOf(f), c = chapterOf(f), siblings = inChapter(c.id), i = siblings.indexOf(f);
      var all = D.features, gi = all.indexOf(f), prev = all[gi - 1], next = all[gi + 1];
      var t = theme(), box = markOf(f);
      document.title = f.title + " · TVE feature guide";
      out.innerHTML =
        '<nav class="crumbs" aria-label="Breadcrumb"><a href="index.html#' + a.id + '">' + a.n + ". " + esc(a.name) + "</a>" + icon("chevron-right") +
        '<a href="features.html#' + c.id + '">' + esc(c.name) + "</a>" + icon("chevron-right") + '<span aria-current="page">' + esc(f.title) + "</span></nav>" +
        '<header class="feature-head"><h1>' + esc(f.title) + "</h1>" + badgeHtml(f) +
        '<p class="lede">' + esc(f.summary) + "</p>" +
        '<div class="where"><strong>Where to find it</strong><span class="path">' + f.where.map(function (p, k) {
          return (k ? icon("chevron-right") : "") + '<span class="seg">' + esc(p) + "</span>";
        }).join("") + "</span></div></header>" +
        '<figure class="shot">' +
        '<img class="shot-close" id="shot-close" src="' + shot(f.id, "focus", t) + '" alt="' + esc(f.title) + ', close up" width="800" height="450">' +
        '<a class="shot-frame" id="shot-link" href="' + shot(f.image, "full", t) + '" aria-label="Open the full-size screenshot">' +
        '<img id="shot-img" data-fixed src="' + shot(f.image, "full", t) + '" alt="' + esc(f.title) + ' in the TechieVideoEditor window" width="1600" height="860">' +
        (box ? '<span class="focus-box" style="left:' + box[0] + "%;top:" + box[1] + "%;width:" + box[2] + "%;height:" + box[3] + '%"></span>' : "") + "</a>" +
        '<figcaption class="shot-bar"><span>Screenshot from the app' + (box ? "; the outline marks where to look" : "") + '. Click it for full size.</span><span class="seg-ctl" role="group" aria-label="Screenshot theme">' +
        '<button type="button" data-t="dark" aria-pressed="' + (t === "dark") + '">Dark</button><button type="button" data-t="light" aria-pressed="' + (t === "light") + '">Light</button></span></figcaption></figure>' +
        '<div class="how"><section><h2>How to use it</h2><ol class="steps">' + f.steps.map(function (s) { return "<li><span>" + esc(s) + "</span></li>"; }).join("") + "</ol></section>" +
        '<aside class="aside-box">' +
        (f.keys ? '<div class="box"><h3>Keys</h3><div class="keys">' + f.keys.map(function (k) { return "<kbd>" + esc(k) + "</kbd>"; }).join("") + "</div></div>" : "") +
        (f.note ? '<div class="box"><h3>Good to know</h3><p>' + esc(f.note) + "</p></div>" : "") +
        (f.privacy === "mac" ? '<div class="box"><h3>Privacy</h3><p>Runs on your Mac. Nothing is uploaded.</p></div>' : "") +
        (f.privacy === "key" ? '<div class="box"><h3>Privacy and cost</h3><p>Uses your own key, added in AI settings. What you send goes to that provider and is billed to your account.</p></div>' : "") +
        '<div class="box"><h3>Topic</h3><p><a href="features.html#' + c.id + '">' + esc(c.name) + "</a> · " + (i + 1) + " of " + siblings.length + "</p></div>" +
        "</aside></div>" +
        '<nav class="pager" aria-label="Previous and next feature">' +
        (prev ? '<a href="' + featureUrl(prev) + '"><small>Previous</small>' + esc(prev.title) + "</a>" : "<span></span>") +
        (next ? '<a class="next" href="' + featureUrl(next) + '"><small>Next</small>' + esc(next.title) + "</a>" : "<span></span>") + "</nav>" +
        '<section class="related" aria-labelledby="rel-h"><h2 id="rel-h">Also in the ' + esc(a.name.toLowerCase()) + "</h2>" +
        '<div class="cards">' + inArea(a.id).filter(function (x) { return x !== f; }).slice(0, 4).map(cardHtml).join("") + "</div></section>";

      $$(".seg-ctl button", out).forEach(function (b) {
        b.addEventListener("click", function () {
          var want = b.getAttribute("data-t");
          $("#shot-img").src = shot(f.image, "full", want);
          $("#shot-link").href = shot(f.image, "full", want);
          $("#shot-close").src = shot(f.id, "focus", want);
          $$(".seg-ctl button", out).forEach(function (o) { o.setAttribute("aria-pressed", String(o === b)); });
        });
      });
      window.scrollTo(0, 0);
    }
    draw();
    window.addEventListener("hashchange", draw);
    document.addEventListener("themechange", draw);
  }

  // ---------- shortcuts ----------
  function renderShortcuts() {
    $("#key-groups").innerHTML = D.shortcuts.map(function (g) {
      return '<section class="key-group"><h2>' + esc(g.group) + "</h2><table>" + g.rows.map(function (r) {
        var keys = r[0].split(" / ").map(function (k) {
          return k.split(" · ").map(function (x) { return "<kbd>" + esc(x) + "</kbd>"; }).join(" ");
        }).join(' <span style="color:var(--text-2)">/</span> ');
        return "<tr><td>" + keys + "</td><td>" + esc(r[1]) + "</td></tr>";
      }).join("") + "</table></section>";
    }).join("");
  }

  // ---------- copy buttons on code ----------
  $$("pre").forEach(function (pre) {
    var b = document.createElement("button");
    b.type = "button"; b.className = "copy"; b.textContent = "Copy";
    b.addEventListener("click", function () {
      var text = $("code", pre) ? $("code", pre).textContent : pre.textContent;
      var done = function () { b.textContent = "Copied"; setTimeout(function () { b.textContent = "Copy"; }, 1600); };
      if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(done, function () { b.textContent = "Select and copy"; });
      else b.textContent = "Select and copy";
    });
    pre.appendChild(b);
  });

  if (page === "map") renderMap();
  else if (page === "all") renderAll();
  else if (page === "feature") renderFeature();
  else if (page === "shortcuts") renderShortcuts();
})();
