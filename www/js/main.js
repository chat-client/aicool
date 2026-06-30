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
      ? "webcool · 文酷 — 私有文件管理生态"
      : "webcool — Private File Management Ecosystem";
  }

  var saved = localStorage.getItem(STORAGE_KEY);
  setLang(saved === "en" ? "en" : "zh");

  document.querySelectorAll(".lang-toggle button").forEach(function (btn) {
    btn.addEventListener("click", function () {
      setLang(btn.dataset.lang);
    });
  });

  var header = document.querySelector(".site-header");
  window.addEventListener("scroll", function () {
    header.classList.toggle("scrolled", window.scrollY > 8);
  }, { passive: true });

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

  document.querySelectorAll(".showcase-tab").forEach(function (tab) {
    tab.addEventListener("click", function () {
      var target = tab.dataset.panel;
      document.querySelectorAll(".showcase-tab").forEach(function (t) {
        t.classList.toggle("active", t === tab);
      });
      document.querySelectorAll(".showcase-panel").forEach(function (panel) {
        panel.classList.toggle("active", panel.id === target);
      });
    });
  });

  var observer = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (entry.isIntersecting) {
        entry.target.style.opacity = "1";
        entry.target.style.transform = "translateY(0)";
      }
    });
  }, { threshold: 0.12, rootMargin: "0px 0px -40px 0px" });

  document.querySelectorAll(".feature-card, .product-card, .step, .platform-card, .workflow-step, .scenario-card, .album-sync-card").forEach(function (el) {
    el.style.opacity = "0";
    el.style.transform = "translateY(16px)";
    el.style.transition = "opacity 0.5s ease, transform 0.5s ease";
    observer.observe(el);
  });
})();
