(function () {
  "use strict";

  var STORAGE_KEY = "webcool-lang";
  var html = document.documentElement;

  function setLang(lang) {
    html.lang = lang;
    localStorage.setItem(STORAGE_KEY, lang);
    document.querySelectorAll(".lang-toggle button").forEach(function (btn) {
      btn.classList.toggle("active", btn.dataset.lang === lang);
    });
    document.title = lang === "zh"
      ? "下载 webcool · 文酷"
      : "Download webcool";
  }

  var saved = localStorage.getItem(STORAGE_KEY);
  setLang(saved === "en" ? "en" : "zh");

  document.querySelectorAll(".lang-toggle button").forEach(function (btn) {
    btn.addEventListener("click", function () {
      setLang(btn.dataset.lang);
    });
  });

  var header = document.querySelector(".site-header");
  if (header) {
    window.addEventListener("scroll", function () {
      header.classList.toggle("scrolled", window.scrollY > 8);
    }, { passive: true });
  }

  var menuToggle = document.querySelector(".menu-toggle");
  var navLinks = document.querySelector(".nav-links");
  if (menuToggle && navLinks) {
    menuToggle.addEventListener("click", function () {
      navLinks.classList.toggle("open");
    });
    navLinks.querySelectorAll("a").forEach(function (link) {
      link.addEventListener("click", function () {
        navLinks.classList.remove("open");
      });
    });
  }

  function bindDownloadLinks(data) {
    var version = data.version || "1.0.0";
    var release = data.release || "1";
    var updated = data.updated || "";
    var files = data.files || {};

    var versionEl = document.getElementById("release-version");
    var dateEl = document.getElementById("release-date");
    if (versionEl) {
      versionEl.textContent = "v" + version + (release !== "1" && release !== version ? "-" + release : "");
    }
    if (dateEl && updated) {
      dateEl.textContent = updated;
    }

    document.querySelectorAll("[data-download-key]").forEach(function (link) {
      var key = link.getAttribute("data-download-key");
      var file = files[key];
      if (!file) {
        link.classList.add("is-disabled");
        link.setAttribute("aria-disabled", "true");
        link.removeAttribute("href");
        return;
      }
      link.href = "https://download.webcool.cn/download/files/" + encodeURIComponent(file);
      //link.href = "./files/" + encodeURIComponent(file);
      link.setAttribute("download", file);
    });
  }

  fetch("./releases.json", { cache: "no-cache" })
    .then(function (res) {
      if (!res.ok) throw new Error("releases.json");
      return res.json();
    })
    .then(bindDownloadLinks)
    .catch(function () {
      bindDownloadLinks({ version: "1.0.0", release: "1", files: {} });
    });
})();
