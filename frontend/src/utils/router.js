const routes = new Set(["dashboard", "wifi", "config"]);

export function getRoute() {
  const raw = window.location.hash.replace("#", "");
  return routes.has(raw) ? raw : "dashboard";
}

export function setRoute(route) {
  const target = routes.has(route) ? route : "dashboard";
  window.location.hash = `#${target}`;
}

export function subscribeRouteChange(handler) {
  const listener = () => handler(getRoute());
  window.addEventListener("hashchange", listener);
  return () => window.removeEventListener("hashchange", listener);
}
