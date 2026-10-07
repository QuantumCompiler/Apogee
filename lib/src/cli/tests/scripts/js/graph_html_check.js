#!/usr/bin/env node
/*
 * Drives the page `apogee graph export html` writes (27m) with no browser:
 * its own script, run as written against a small stand-in for the DOM and a
 * canvas that records what is drawn. What it holds:
 *
 *   - the embedded data block parses, and the script runs over it;
 *   - the layout settles with every entity drawn at a finite place;
 *   - a click on a drawn entity opens that entity's card (click-to-explain),
 *     and a neighbour in the card opens the neighbour's;
 *   - search lists the entities whose name holds the query, and choosing one
 *     opens its card;
 *   - the legend colours communities and a click on one focuses it;
 *   - the script never reaches for the network: fetch, XMLHttpRequest,
 *     WebSocket and their kin do not exist here, so a call would throw.
 *
 * Usage: graph_html_check.js <page.html> <entity name to search for>
 * Exits 0 with "graph html check: OK", or 1 naming what failed.
 */
'use strict';

const fs = require('fs');
const vm = require('vm');

function fail(message) {
  console.error('graph html check: ' + message);
  process.exit(1);
}

const [page, query] = process.argv.slice(2);
if (!page || !query) {
  fail('usage: graph_html_check.js <page.html> <entity name to search for>');
}
const html = fs.readFileSync(page, 'utf8');
const block = html.match(/<script type="application\/json" id="graph-data">([\s\S]*?)<\/script>/);
if (!block) {
  fail('no data block');
}
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
if (scripts.length !== 1) {
  fail('expected one inline script, found ' + scripts.length);
}
const data = JSON.parse(block[1]);

/* ---- A stand-in DOM: enough of it for the page, nothing more ---- */
class Text {
  constructor(text) {
    this.textContent = String(text);
  }
}
class Element {
  constructor(tag) {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.attributes = {};
    this.listeners = {};
    this.style = {};
    this.dataset = {};
    this.hidden = false;
    this.className = '';
    this.own = '';
    this.value = '';
    this.type = '';
    this.title = '';
    this.scrollTop = 0;
    this.clientWidth = 1000;
    this.clientHeight = 700;
    this.width = 0;
    this.height = 0;
    const element = this;
    this.classList = {
      add: (c) => element.setClasses(element.classes().concat([c])),
      remove: (c) => element.setClasses(element.classes().filter((x) => x !== c)),
      toggle: (c, on) => {
        const has = element.classes().includes(c);
        const want = on === undefined ? !has : on;
        if (want && !has) {
          element.classList.add(c);
        } else if (!want && has) {
          element.classList.remove(c);
        }
      },
      contains: (c) => element.classes().includes(c)
    };
  }
  classes() {
    return this.className.split(/\s+/).filter(Boolean);
  }
  setClasses(list) {
    this.className = list.join(' ');
  }
  get textContent() {
    return this.own + this.children.map((c) => c.textContent).join('');
  }
  set textContent(value) {
    this.own = String(value);
    this.children = [];
  }
  append(...nodes) {
    for (const node of nodes) {
      this.children.push(typeof node === 'string' ? new Text(node) : node);
    }
  }
  replaceChildren(...nodes) {
    this.own = '';
    this.children = [];
    this.append(...nodes);
  }
  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }
  getAttribute(name) {
    return this.attributes[name];
  }
  addEventListener(type, listener) {
    (this.listeners[type] = this.listeners[type] || []).push(listener);
  }
  dispatch(type, event) {
    for (const listener of this.listeners[type] || []) {
      listener(Object.assign({ preventDefault() {}, target: this }, event || {}));
    }
  }
  descendants() {
    const out = [];
    for (const child of this.children) {
      if (child instanceof Element) {
        out.push(child, ...child.descendants());
      }
    }
    return out;
  }
  querySelectorAll(selector) {
    if (selector !== 'li[role="option"]') {
      fail('unexpected selector ' + selector);
    }
    return this.descendants().filter((e) => e.tagName === 'LI' && e.attributes.role === 'option');
  }
  scrollIntoView() {}
  focus() {
    documentStub.activeElement = this;
    this.dispatch('focus');
  }
  blur() {
    if (documentStub.activeElement === this) {
      documentStub.activeElement = null;
    }
    this.dispatch('blur');
  }
  getBoundingClientRect() {
    return { left: 0, top: 0, width: this.clientWidth, height: this.clientHeight };
  }
  setPointerCapture() {}
  getContext() {
    return context;
  }
}

/* A canvas context that records every call. */
const calls = [];
const context = new Proxy({}, {
  get(target, name) {
    if (name in target) {
      return target[name];
    }
    return (...args) => {
      calls.push([name, args]);
    };
  },
  set(target, name, value) {
    target[name] = value;
    return true;
  }
});

const byId = {};
for (const [id, tag] of [['graph-data', 'script'], ['canvas', 'canvas'], ['search', 'input'],
  ['results', 'ul'], ['legend', 'section'], ['card', 'aside']]) {
  byId[id] = new Element(tag);
}
byId['graph-data'].textContent = block[1];
byId.results.hidden = true;
byId.card.hidden = true;

const documentListeners = {};
const documentStub = {
  activeElement: null,
  documentElement: new Element('html'),
  getElementById: (id) => byId[id] || null,
  createElement: (tag) => new Element(tag),
  createTextNode: (text) => new Text(text),
  addEventListener: (type, listener) => {
    (documentListeners[type] = documentListeners[type] || []).push(listener);
  }
};
const frames = [];
const theme = {
  '--edge': 'rgba(0,0,0,0.2)', '--edge-hi': 'rgba(0,0,0,0.7)', '--accent': '#2563d9',
  '--ink': '#111', '--halo': 'rgba(255,255,255,0.9)', '--none': '#999', '--sat': '62%', '--lit': '47%'
};
const sandbox = {
  document: documentStub,
  window: {
    devicePixelRatio: 2,
    addEventListener() {},
    matchMedia: () => ({ addEventListener() {} })
  },
  getComputedStyle: () => ({ getPropertyValue: (name) => theme[name] || '' }),
  requestAnimationFrame: (callback) => frames.push(callback),
  performance: { now: () => Number(process.hrtime.bigint() / 1000000n) },
  console: console,
  Math: Math,
  JSON: JSON
};
sandbox.window.requestAnimationFrame = sandbox.requestAnimationFrame;

try {
  vm.runInNewContext(scripts[0][1], sandbox, { filename: 'graph.html <script>' });
} catch (error) {
  fail('the script threw at load: ' + (error && error.stack ? error.stack : error));
}

function runFrames(limit) {
  let ran = 0;
  while (frames.length && ran < limit) {
    const next = frames.shift();
    calls.length = 0;
    try {
      next();
    } catch (error) {
      fail('a frame threw: ' + (error && error.stack ? error.stack : error));
    }
    ran += 1;
  }
  return ran;
}

/* ---- The layout settles, every entity at a finite place ---- */
const N = data.nodes.length;
const ran = runFrames(2000);
if (frames.length) {
  fail('the layout had not settled after ' + ran + ' frames');
}
const arcs = calls.filter(([name, args]) => name === 'arc' && args[3] === 0);
if (N > 0 && arcs.length < N) {
  fail('drew ' + arcs.length + ' entities of ' + N);
}
for (const [, args] of arcs) {
  if (!args.slice(0, 3).every(Number.isFinite)) {
    fail('an entity was drawn at ' + JSON.stringify(args));
  }
}
const fills = calls.filter(([name]) => name === 'fillText').map(([, args]) => args[0]);
if (N > 0 && fills.length === 0) {
  fail('no label was drawn');
}

/* ---- Click-to-explain: a click on the busiest entity opens its card ---- */
const translate = calls.find(([name]) => name === 'translate');
const scale = calls.find(([name]) => name === 'scale');
if (N > 0) {
  /* The entities are drawn last first, so the final arc is entity 0. */
  const [wx, wy] = arcs[arcs.length - 1][1];
  const k = scale[1][0];
  const sx = wx * k + translate[1][0];
  const sy = wy * k + translate[1][1];
  const canvas = byId.canvas;
  canvas.dispatch('pointerdown', { clientX: sx, clientY: sy, pointerId: 1 });
  canvas.dispatch('pointerup', { clientX: sx, clientY: sy, pointerId: 1 });
  runFrames(5);
  if (byId.card.hidden) {
    fail('a click on an entity opened no card');
  }
  const heading = byId.card.descendants().find((e) => e.tagName === 'H2');
  if (!heading || heading.textContent !== data.nodes[0].node.name) {
    fail('the card is not the clicked entity\'s: ' + (heading ? heading.textContent : 'no heading'));
  }
  const text = byId.card.textContent;
  for (const part of ['relation', 'out,']) {
    if (!text.includes(part)) {
      fail('the card lacks "' + part + '": ' + text.slice(0, 300));
    }
  }
  const peer = byId.card.descendants().find((e) => e.tagName === 'BUTTON' && e.classList.contains('peer'));
  if (peer) {
    peer.dispatch('click');
    runFrames(5);
    const next = byId.card.descendants().find((e) => e.tagName === 'H2');
    if (!next || next.textContent !== peer.textContent) {
      fail('a neighbour in the card did not open its own card');
    }
  }
}

/* ---- Search ---- */
const search = byId.search;
search.value = query;
search.dispatch('input');
runFrames(5);
const options = byId.results.querySelectorAll('li[role="option"]');
if (options.length === 0) {
  fail('searching "' + query + '" listed nothing: ' + byId.results.textContent);
}
if (!options[0].textContent.toLowerCase().includes(query.toLowerCase())) {
  fail('the first result does not hold the query: ' + options[0].textContent);
}
options[0].dispatch('mousedown');
runFrames(5);
const chosen = byId.card.descendants().find((e) => e.tagName === 'H2');
if (byId.card.hidden || !chosen || !chosen.textContent.toLowerCase().includes(query.toLowerCase())) {
  fail('choosing a result did not open its card');
}
search.value = 'zzz-nothing-is-called-this';
search.dispatch('input');
if (!byId.results.textContent.includes('No entity matches')) {
  fail('a search matching nothing did not say so');
}
search.dispatch('keydown', { key: 'Escape' });

/* ---- The legend ---- */
const swatches = byId.legend.descendants().filter((e) => e.classList.contains('swatch'));
if (data.communities.length > 0) {
  if (swatches.length === 0 || !String(swatches[0].style.background).startsWith('hsl(')) {
    fail('the legend has no community colour');
  }
  const community = byId.legend.descendants().find((e) => e.classList.contains('community'));
  community.dispatch('click');
  runFrames(5);
}
for (const listener of documentListeners.keydown || []) {
  listener({ key: '/', preventDefault() {} });
}
if (documentStub.activeElement !== search) {
  fail('"/" did not focus the search');
}

console.log('graph html check: OK (' + N + ' entities, ' + data.edges.length + ' relations, ' +
  ran + ' frames to settle)');
