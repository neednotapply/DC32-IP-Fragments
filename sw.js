/* Badge Studio service worker.
 *
 * The point of this is a conference floor: the hall wifi is saturated, you are
 * standing at a table with a badge and a USB cable, and you want the studio to
 * come up and flash. So the shell and the firmware parts are both cached, and
 * every strategy below is chosen to keep a stale build from ever reaching a
 * badge - fresh when the network answers, last-known-good when it does not.
 */

/* Bump this whenever a file under assets/ changes, or the shell is reshaped.
 * Pages and firmware are fetched network-first so they refresh on their own,
 * but assets are served cache-first: without a new cache name a returning
 * visitor keeps the old icons forever. (v1 -> v2 retired the gold mark;
 * v2 -> v3 folded studio.html into index.html; v3 -> v4 took the opaque
 * plate off the icons; v4 -> v5 moved the flash manifest to firmware.json.) */
const VERSION = 'badge-studio-v5';
const SHELL    = `${VERSION}-shell`;
const RUNTIME  = `${VERSION}-runtime`;
const KEEP     = new Set([SHELL, RUNTIME]);

/* Fails the install if any of these are missing - without them there is no app. */
const CORE = [
  './',
  './index.html',
  './manifest.webmanifest',
  './assets/logo-mark.png',
  './assets/favicon.ico',
];

/* Best-effort: a missing icon or firmware part should not cost us the whole
 * offline install, so these are fetched individually and allowed to fail. */
const EXTRA = [
  './assets/icon-192.png',
  './assets/icon-512.png',
  './assets/apple-touch-icon.png',
  './firmware.json',
  './firmware/bootloader.bin',
  './firmware/partitions.bin',
  './firmware/boot_app0.bin',
  './firmware/application.bin',
];

const NAV_TIMEOUT_MS = 3000;

self.addEventListener('install', event => {
  event.waitUntil((async () => {
    const cache = await caches.open(SHELL);
    await cache.addAll(CORE);
    await Promise.allSettled(EXTRA.map(url => cache.add(url)));
    await self.skipWaiting();
  })());
});

self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.map(n => KEEP.has(n) ? null : caches.delete(n)));
    await self.clients.claim();
  })());
});

/* Network first, but never hang on it: on a dead network the cached copy is
 * served after NAV_TIMEOUT_MS rather than after the browser's own timeout. */
async function networkFirst(request, cacheName, timeoutMs) {
  const cache = await caches.open(cacheName);
  const cached = await cache.match(request);

  const network = fetch(request).then(res => {
    if (res && res.ok) cache.put(request, res.clone()).catch(() => {});
    return res;
  });
  // A timed-out race abandons this promise while it is still in flight; give it
  // a handler now so a later failure is not reported as an unhandled rejection.
  network.catch(() => {});

  if (!cached) return network.catch(() => offlineFallback(request));

  try {
    if (!timeoutMs) return await network;
    let timer;
    const expiry = new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error('slow network')), timeoutMs);
    });
    try { return await Promise.race([network, expiry]); }
    finally { clearTimeout(timer); }
  } catch {
    return cached;
  }
}

/* Cache first for things whose URL changes when their content does. */
async function cacheFirst(request, cacheName) {
  const cache = await caches.open(cacheName);
  const cached = await cache.match(request);
  if (cached) return cached;

  const res = await fetch(request);
  // Opaque responses (no-cors) report status 0; they are still worth keeping.
  if (res && (res.ok || res.type === 'opaque')) cache.put(request, res.clone()).catch(() => {});
  return res;
}

async function offlineFallback(request) {
  if (request.mode === 'navigate') {
    const shell = await caches.open(SHELL);
    const page = await shell.match('./index.html');
    if (page) return page;
  }
  return Response.error();
}

self.addEventListener('fetch', event => {
  const { request } = event;
  if (request.method !== 'GET') return;

  const url = new URL(request.url);
  const sameOrigin = url.origin === self.location.origin;

  // Pages: fresh build when the network answers within NAV_TIMEOUT_MS.
  if (request.mode === 'navigate' || (sameOrigin && url.pathname.endsWith('.html'))) {
    event.respondWith(networkFirst(request, SHELL, NAV_TIMEOUT_MS));
    return;
  }

  // Firmware: the one thing that must never be silently stale, because a stale
  // .bin gets written to a real badge. Cache is a fallback, never a shortcut.
  if (sameOrigin && url.pathname.includes('/firmware/')) {
    event.respondWith(networkFirst(request, SHELL, 0));
    return;
  }

  if (sameOrigin && url.pathname.includes('/assets/')) {
    event.respondWith(cacheFirst(request, SHELL));
    return;
  }

  // Version-pinned CDN payloads: esp-web-tools and the webfonts. Caching these
  // is what lets a second visit come up with no network at all.
  if (url.host === 'unpkg.com' ||
      url.host === 'fonts.googleapis.com' ||
      url.host === 'fonts.gstatic.com') {
    event.respondWith(cacheFirst(request, RUNTIME).catch(() => offlineFallback(request)));
    return;
  }

  // Everything else we serve ourselves - the manifest today, whatever gets
  // added later - still deserves a cached copy to fall back on. Without this
  // the manifest is precached but never served from it, so an offline install
  // has no manifest to read.
  if (sameOrigin) {
    event.respondWith(networkFirst(request, SHELL, 0));
  }
});
