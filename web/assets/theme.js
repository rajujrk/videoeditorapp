/* Applies the saved theme before the page paints, so it never flashes the other one. */
(function () {
  try {
    var saved = localStorage.getItem("tve-guide-theme");
    if (saved === "dark" || saved === "light") document.documentElement.setAttribute("data-theme", saved);
  } catch (e) { /* storage blocked: follow the system */ }
})();
