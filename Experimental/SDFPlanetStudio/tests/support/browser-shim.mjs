/**
 * Minimal browser shim: enough DOM, event plumbing and animation plumbing to
 * construct the editor and pump frames headlessly.
 *
 * Elements are plain objects with the surface the app touches (classList, dataset,
 * style, children, listeners, text). No layout, no rendering: a `clientWidth` of
 * 1600 is invented so the camera maths has something to divide by.
 */

class ClassList {
  constructor() {
    this.set = new Set();
  }
  add(...names) {
    for (const name of names) this.set.add(name);
  }
  remove(...names) {
    for (const name of names) this.set.delete(name);
  }
  toggle(name, force) {
    const on = force === undefined ? !this.set.has(name) : Boolean(force);
    if (on) this.set.add(name);
    else this.set.delete(name);
    return on;
  }
  contains(name) {
    return this.set.has(name);
  }
}

class ElementStub {
  constructor(tag = 'div', id = '') {
    this.tagName = tag.toUpperCase();
    this.id = id;
    this.dataset = {};
    this.style = {};
    this.classList = new ClassList();
    this.children = [];
    this.listeners = new Map();
    this.textContent = '';
    this.value = '';
    this.placeholder = '';
    this.files = [];
    this.parentElement = null;
    this.clientWidth = 1600;
    this.clientHeight = 900;
    this.innerHTML = '';
    this.attributes = new Map();
  }

  set className(value) {
    this.classList.set = new Set(String(value).split(/\s+/).filter(Boolean));
  }
  get className() {
    return [...this.classList.set].join(' ');
  }

  addEventListener(type, handler) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(handler);
  }
  removeEventListener(type, handler) {
    const list = this.listeners.get(type) ?? [];
    const index = list.indexOf(handler);
    if (index >= 0) list.splice(index, 1);
  }
  dispatch(type, event = {}) {
    for (const handler of this.listeners.get(type) ?? []) handler({ target: this, ...event });
  }
  appendChild(child) {
    this.children.push(child);
    child.parentElement = this;
    return child;
  }
  removeChild(child) {
    const index = this.children.indexOf(child);
    if (index >= 0) this.children.splice(index, 1);
    return child;
  }
  querySelector(selector) {
    return findElement(this, selector);
  }
  querySelectorAll(selector) {
    return collectElements(this, selector);
  }
  setAttribute(name, value) {
    this.attributes.set(name, value);
  }
  getAttribute(name) {
    return this.attributes.get(name) ?? null;
  }
  getBoundingClientRect() {
    return { left: 0, top: 0, width: this.clientWidth, height: this.clientHeight, right: this.clientWidth, bottom: this.clientHeight };
  }
  setPointerCapture() {}
  releasePointerCapture() {}
  focus() {}
  click() {
    this.dispatch('click', {});
  }
  closest() {
    return this;
  }
}

/** Depth-first search over the stub tree, matching `#id` and `[data-x]` only. */
function findElement(root, selector) {
  const all = collectElements(root, selector);
  return all[0] ?? new ElementStub('div', selector.replace(/^#/, ''));
}

function collectElements(root, selector) {
  const matches = [];
  const wanted = String(selector).trim();
  const walk = (node) => {
    for (const child of node.children) {
      if (matchesSelector(child, wanted)) matches.push(child);
      walk(child);
    }
  };
  walk(root);
  return matches;
}

function matchesSelector(element, selector) {
  if (selector.startsWith('#')) return element.id === selector.slice(1);
  if (selector === '.tp-tool') return element.classList.contains('tp-tool');
  if (selector === 'input') return element.tagName === 'INPUT';
  if (selector.startsWith('input[')) return element.tagName === 'INPUT';
  if (selector.startsWith('[data-')) {
    const key = selector.slice(6, selector.indexOf(']'));
    return Object.prototype.hasOwnProperty.call(element.dataset, key);
  }
  return false;
}

export function installBrowserShim() {
  const registry = new Map();
  const documentElement = new ElementStub('html');

  const canvas = new ElementStub('canvas', 'viewport-canvas');
  canvas.clientWidth = 1600;
  canvas.clientHeight = 900;

  const document = {
    documentElement,
    body: new ElementStub('body'),
    activeElement: null,
    querySelector(selector) {
      if (selector === '#viewport-canvas') return canvas;
      if (!registry.has(selector)) registry.set(selector, new ElementStub('div', selector.replace(/^#/, '')));
      return registry.get(selector);
    },
    querySelectorAll(selector) {
      return collectElements(documentElement, selector);
    },
    createElement(tag) {
      return new ElementStub(tag);
    },
    addEventListener() {},
    removeEventListener() {},
  };

  const rafQueue = [];
  const timeouts = [];

  const windowListeners = new Map();

  const window = {
    devicePixelRatio: 1,
    innerWidth: 1600,
    innerHeight: 900,
    addEventListener(type, handler) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(handler);
    },
    removeEventListener(type, handler) {
      const list = windowListeners.get(type) ?? [];
      const index = list.indexOf(handler);
      if (index >= 0) list.splice(index, 1);
    },
    requestAnimationFrame(callback) {
      rafQueue.push(callback);
      return rafQueue.length;
    },
    cancelAnimationFrame() {},
    setTimeout(callback) {
      timeouts.push(callback);
      return timeouts.length;
    },
    clearTimeout() {},
    getComputedStyle() {
      return { getPropertyValue: () => '' };
    },
  };

  class ResizeObserverStub {
    constructor(callback) {
      this.callback = callback;
    }
    observe() {}
    unobserve() {}
    disconnect() {}
  }

  class WorkerStub {
    constructor(url) {
      this.url = url;
      this.onmessage = null;
      this.onerror = null;
      this.posted = [];
      WorkerStub.instances.push(this);
    }
    postMessage(message) {
      this.posted.push(message);
    }
    terminate() {}
  }
  WorkerStub.instances = [];

  const globalRef = globalThis;
  const define = (name, value) => {
    // Some globals (navigator) exist on Node as getter-only properties.
    Object.defineProperty(globalRef, name, { value, writable: true, configurable: true });
  };

  define('window', window);
  define('document', document);
  define('navigator', { hardwareConcurrency: 8, userAgent: 'node-smoke' });
  define('ResizeObserver', ResizeObserverStub);
  define('Worker', WorkerStub);
  define('requestAnimationFrame', window.requestAnimationFrame);
  define('cancelAnimationFrame', window.cancelAnimationFrame);
  define('devicePixelRatio', 1);

  return {
    window,
    document,
    canvas,
    WorkerStub,
    /** Fire a window-level event (keyboard shortcuts and resize). */
    fireWindow(type, event = {}) {
      for (const handler of [...(windowListeners.get(type) ?? [])]) {
        handler({ type, target: { tagName: 'BODY' }, preventDefault() {}, ...event });
      }
    },
    /** Run `count` animation frames. */
    pump(count = 1) {
      for (let i = 0; i < count; i++) {
        const callbacks = rafQueue.splice(0, rafQueue.length);
        for (const callback of callbacks) callback(performance.now());
      }
    },
    get pendingFrames() {
      return rafQueue.length;
    },
  };
}
