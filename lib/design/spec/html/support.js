/*
 * A standalone runtime for the design's .dc.html files, so they open in an
 * ordinary browser outside the design canvas (which serves its own support.js
 * in place of this file).
 *
 * It reads a file's <x-dc> template and its <script type="text/x-dc"> logic,
 * fills the template from the Component's renderVals(), and patches the page
 * whenever setState runs. It supports what the design files use: {{dotted.holes}},
 * <sc-if>, <sc-for>, <dc-import>, <helmet>, event attributes (onClick, onChange,
 * onKeyDown, ...), and the data-props defaults, overridden from the URL
 * (main.html?start=video&still=true).
 *
 * Opened from disk (file://), the page renders from its own parsed DOM. Served
 * over http, it reads its own source, which is exact, and <dc-import> works;
 * ?source=dom renders from the DOM there too, as a page opened from disk does.
 */
(function () {
  'use strict';

  var hide = document.createElement('style');
  hide.textContent = 'x-dc{display:none!important}';
  (document.head || document.documentElement).appendChild(hide);

  var SVG_NS = 'http://www.w3.org/2000/svg';
  var VOID = { area: 1, base: 1, br: 1, col: 1, embed: 1, hr: 1, img: 1, input: 1, link: 1, meta: 1, source: 1, track: 1, wbr: 1 };
  var RAW = { script: 1, style: 1, textarea: 1, title: 1 };
  var BOOL = { disabled: 1, checked: 1, readonly: 1, selected: 1, multiple: 1, required: 1, hidden: 1, autofocus: 1 };
  var SVG_CASE = { viewbox: 'viewBox', preserveaspectratio: 'preserveAspectRatio', markerwidth: 'markerWidth', markerheight: 'markerHeight', refx: 'refX', refy: 'refY', markerunits: 'markerUnits' };
  // A board's <dc-import name="App"> means App.dc.html on the canvas; in the
  // repo the prototype is main.html.
  var ALIAS = { App: 'main.html' };

  /* ---------- the component base ---------- */

  function DCLogic(props) {
    this.props = props || {};
    this.state = {};
  }
  DCLogic.prototype.setState = function (patch, done) {
    var next = typeof patch === 'function' ? patch(this.state, this.props) : patch;
    if (next) this.state = Object.assign({}, this.state, next);
    if (this.__host) this.__host.schedule();
    if (done) Promise.resolve().then(done);
  };
  DCLogic.prototype.forceUpdate = function () {
    if (this.__host) this.__host.schedule();
  };
  window.DCLogic = DCLogic;

  /* ---------- reading a template ---------- */

  var decoder = document.createElement('textarea');
  function decode(s) {
    if (s.indexOf('&') < 0) return s;
    decoder.innerHTML = s;
    return decoder.value;
  }

  var nextId = 0;
  function parse(src) {
    var root = { tag: '#root', children: [] };
    var stack = [root];
    var i = 0;
    var n = src.length;
    while (i < n) {
      var top = stack[stack.length - 1];
      if (src.startsWith('<!--', i)) {
        var ce = src.indexOf('-->', i);
        i = ce < 0 ? n : ce + 3;
        continue;
      }
      if (src[i] === '<' && src[i + 1] === '/') {
        var ee = src.indexOf('>', i);
        var closing = src.slice(i + 2, ee).trim().toLowerCase();
        for (var j = stack.length - 1; j > 0; j -= 1) {
          if (stack[j].tag === closing) { stack.length = j; break; }
        }
        i = ee + 1;
        continue;
      }
      if (src[i] === '<' && /[A-Za-z]/.test(src[i + 1] || '')) {
        var k = i + 1;
        while (k < n && /[^\s/>]/.test(src[k])) k += 1;
        var raw = src.slice(i + 1, k);
        var node = { id: nextId++, tag: raw.toLowerCase(), raw: raw, attrs: [], children: [] };
        var self = false;
        while (k < n) {
          while (k < n && /\s/.test(src[k])) k += 1;
          if (src[k] === '>') { k += 1; break; }
          if (src[k] === '/' && src[k + 1] === '>') { self = true; k += 2; break; }
          var a = k;
          while (k < n && /[^\s=>]/.test(src[k]) && !(src[k] === '/' && src[k + 1] === '>')) k += 1;
          var name = src.slice(a, k);
          var value = null;
          while (k < n && /\s/.test(src[k])) k += 1;
          if (src[k] === '=') {
            k += 1;
            while (k < n && /\s/.test(src[k])) k += 1;
            var q = src[k];
            if (q === '"' || q === "'") {
              var qe = src.indexOf(q, k + 1);
              value = src.slice(k + 1, qe);
              k = qe + 1;
            } else {
              var ua = k;
              while (k < n && /[^\s>]/.test(src[k])) k += 1;
              value = src.slice(ua, k);
            }
          }
          if (name) node.attrs.push({ name: name, value: value === null ? '' : decode(value), bare: value === null });
        }
        top.children.push(node);
        i = k;
        if (RAW[node.tag]) {
          var re = src.toLowerCase().indexOf('</' + node.tag, i);
          if (re < 0) re = n;
          var body = src.slice(i, re);
          if (body) node.children.push({ id: nextId++, text: node.tag === 'textarea' ? decode(body) : body });
          i = src.indexOf('>', re) + 1 || n;
        } else if (!VOID[node.tag] && !self) {
          stack.push(node);
        }
        continue;
      }
      var te = i + 1;
      while (te < n && !(src[te] === '<' && /[A-Za-z/!]/.test(src[te + 1] || ''))) te += 1;
      top.children.push({ id: nextId++, text: decode(src.slice(i, te)) });
      i = te;
    }
    return root;
  }

  function between(src, open, close) {
    var a = src.indexOf(open);
    if (a < 0) return null;
    a = src.indexOf('>', a) + 1;
    var b = src.lastIndexOf(close);
    return b > a ? src.slice(a, b) : null;
  }

  // One .dc.html file's parts, from its source text.
  function fromSource(src) {
    var tpl = between(src, '<x-dc', '</x-dc>');
    var m = /<script[^>]*data-dc-script[^>]*>([\s\S]*?)<\/script>/i.exec(src);
    var tag = m ? /<script[^>]*>/i.exec(m[0])[0] : '';
    var pm = /data-props\s*=\s*'([^']*)'/.exec(tag) || /data-props\s*=\s*"([^"]*)"/.exec(tag);
    return { template: parse(tpl || ''), code: m ? m[1] : '', props: pm ? decode(pm[1]) : '{}' };
  }

  // The page's own parts, from its live DOM, when its source can't be read.
  function fromDom() {
    var x = document.querySelector('x-dc');
    var s = document.querySelector('script[data-dc-script]');
    return { template: parse(x ? x.innerHTML : ''), code: s ? s.textContent : '', props: (s && s.getAttribute('data-props')) || '{}' };
  }

  /* ---------- filling a template ---------- */

  function lookup(expr, scope) {
    var e = expr.trim();
    if (e === 'true') return true;
    if (e === 'false') return false;
    if (e === 'null') return null;
    if (/^-?\d+(\.\d+)?$/.test(e)) return Number(e);
    if (/^(['"]).*\1$/.test(e)) return e.slice(1, -1);
    var parts = e.split('.');
    var v;
    for (var s = scope; s; s = s.parent) {
      if (Object.prototype.hasOwnProperty.call(s.vars, parts[0])) { v = s.vars[parts[0]]; break; }
    }
    for (var p = 1; p < parts.length && v != null; p += 1) v = v[parts[p]];
    return v;
  }

  var WHOLE = /^\s*\{\{([^}]*)\}\}\s*$/;
  var HOLE = /\{\{([^}]*)\}\}/g;
  function fill(value, scope) {
    var w = WHOLE.exec(value);
    if (w) return lookup(w[1], scope);
    if (value.indexOf('{{') < 0) return value;
    return value.replace(HOLE, function (_, ex) {
      var v = lookup(ex, scope);
      return v == null ? '' : String(v);
    });
  }

  function camel(s) {
    return s.replace(/-([a-z])/g, function (_, c) { return c.toUpperCase(); });
  }

  function attr(node, name) {
    for (var i = 0; i < node.attrs.length; i += 1) if (node.attrs[i].name.toLowerCase() === name) return node.attrs[i];
    return null;
  }

  // The vnodes one template node becomes, appended to out.
  function build(node, scope, key, svg, out, ctx) {
    if (node.text != null) {
      var tv = fill(node.text, scope);
      out.push({ k: key + node.id, text: tv == null || typeof tv === 'boolean' ? '' : String(tv) });
      return;
    }
    var t = node.tag;
    if (t === 'helmet') return;
    if (t === 'sc-if') {
      var cond = attr(node, 'value');
      if (cond && fill(cond.value, scope)) node.children.forEach(function (c) { build(c, scope, key, svg, out, ctx); });
      return;
    }
    if (t === 'sc-for') {
      var listA = attr(node, 'list');
      var asA = attr(node, 'as');
      var list = listA ? fill(listA.value, scope) : null;
      if (!Array.isArray(list)) return;
      var as = asA ? asA.value : 'item';
      list.forEach(function (item, i) {
        var vars = { $index: i };
        vars[as] = item;
        var inner = { vars: vars, parent: scope };
        node.children.forEach(function (c) { build(c, inner, key + node.id + ':' + i + '/', svg, out, ctx); });
      });
      return;
    }
    if (t === 'dc-import') {
      var props = {};
      var file = '';
      node.attrs.forEach(function (a) {
        var nm = a.name.toLowerCase();
        if (nm === 'name') file = ALIAS[a.value] || a.value + '.dc.html';
        else if (nm.indexOf('hint-') !== 0) props[camel(nm)] = a.bare ? true : fill(a.value, scope);
      });
      out.push({ k: key + node.id, tag: 'div', ns: null, attrs: { style: 'display: contents', 'data-dc-import': file }, events: {}, children: [], island: { file: file, props: props } });
      return;
    }
    var inSvg = svg || t === 'svg';
    var v = { k: key + node.id, tag: inSvg ? node.raw : t, ns: inSvg ? SVG_NS : null, attrs: {}, events: {}, children: [] };
    node.attrs.forEach(function (a) {
      var nm = a.name;
      var low = nm.toLowerCase();
      if (low.indexOf('hint-') === 0) return;
      var val = a.bare ? true : fill(a.value, scope);
      if (low.length > 2 && low.indexOf('on') === 0 && typeof val === 'function') {
        var ev = low.slice(2);
        if (ev === 'change' && (t === 'textarea' || (t === 'input' && !/^(checkbox|radio|file)$/i.test((attr(node, 'type') || {}).value || '')))) ev = 'input';
        v.events[ev] = val;
        return;
      }
      if (inSvg && SVG_CASE[low]) nm = SVG_CASE[low];
      v.attrs[inSvg ? nm : low] = val;
    });
    var kids = [];
    node.children.forEach(function (c) { build(c, scope, key, inSvg && t !== 'foreignobject', kids, ctx); });
    v.children = kids;
    out.push(v);
  }

  /* ---------- patching the page ---------- */

  function setAttr(el, name, val, isSvg) {
    if (name === 'value' && ('value' in el) && el.tagName !== 'OPTION' && el.tagName !== 'LI' && el.tagName !== 'BUTTON') {
      el.__value = val;
      return;
    }
    if (BOOL[name.toLowerCase()] && !isSvg) {
      var on = val === true || (val !== false && val != null && val !== '' && val !== 'false');
      var prop = name.toLowerCase() === 'readonly' ? 'readOnly' : name.toLowerCase();
      if (prop in el) el[prop] = on;
      if (on) el.setAttribute(name, ''); else el.removeAttribute(name);
      return;
    }
    if (val == null || val === false && name.indexOf('aria-') !== 0) { el.removeAttribute(name); return; }
    el.setAttribute(name, val === true ? (name.indexOf('aria-') === 0 ? 'true' : '') : String(val));
  }

  function listen(el, type) {
    el.addEventListener(type, function (e) {
      var h = el.__h && el.__h[type];
      if (h) h(e);
    });
  }

  function update(el, v, host) {
    var old = el.__a || {};
    var isSvg = !!v.ns;
    Object.keys(old).forEach(function (n) { if (!(n in v.attrs)) setAttr(el, n, null, isSvg); });
    Object.keys(v.attrs).forEach(function (n) { if (old[n] !== v.attrs[n] || n === 'value') setAttr(el, n, v.attrs[n], isSvg); });
    el.__a = v.attrs;
    el.__l = el.__l || {};
    Object.keys(v.events).forEach(function (t) { if (!el.__l[t]) { el.__l[t] = true; listen(el, t); } });
    el.__h = v.events;
    if (v.island) {
      if (!el.__child) el.__child = mountImport(el, v.island.file, v.island.props, host);
      else el.__child.setProps(v.island.props);
      return;
    }
    patchChildren(el, v.children, host);
    if (el.__value !== undefined) {
      var want = el.__value == null ? '' : String(el.__value);
      if (el.type === 'checkbox' || el.type === 'radio') { /* checked carries it */ }
      else if (el.value !== want) el.value = want;
    }
  }

  function create(v, host) {
    if (v.text != null) {
      var tn = document.createTextNode(v.text);
      tn.__k = v.k;
      return tn;
    }
    var el = v.ns ? document.createElementNS(v.ns, v.tag) : document.createElement(v.tag);
    el.__k = v.k;
    update(el, v, host);
    return el;
  }

  function same(node, v) {
    if (v.text != null) return node.nodeType === 3;
    return node.nodeType === 1 && node.__tag === v.tag + '|' + (v.ns || '');
  }

  function patchChildren(parent, vnodes, host) {
    var existing = {};
    for (var c = parent.firstChild; c; c = c.nextSibling) if (c.__k != null) existing[c.__k] = c;
    var keep = [];
    for (var i = 0; i < vnodes.length; i += 1) {
      var v = vnodes[i];
      var node = existing[v.k];
      if (node && !same(node, v)) node = null;
      if (node) {
        delete existing[v.k];
        if (v.text != null) { if (node.data !== v.text) node.data = v.text; }
        else update(node, v, host);
      } else {
        node = create(v, host);
        if (node.nodeType === 1) node.__tag = v.tag + '|' + (v.ns || '');
      }
      keep.push(node);
      var at = parent.childNodes[i];
      if (at !== node) parent.insertBefore(node, at || null);
    }
    while (parent.childNodes.length > keep.length) parent.removeChild(parent.lastChild);
  }

  /* ---------- a mounted component ---------- */

  var helmets = {};
  function applyHelmet(template) {
    template.children.forEach(function (n) {
      if (n.tag !== 'helmet') return;
      n.children.forEach(function (h) {
        if (!h.tag) return;
        var sig = h.tag + JSON.stringify(h.attrs) + (h.children[0] ? h.children[0].text : '');
        if (helmets[sig]) return;
        helmets[sig] = true;
        var el = document.createElement(h.tag);
        h.attrs.forEach(function (a) { el.setAttribute(a.name, a.value); });
        if (h.children[0] && h.children[0].text) el.textContent = h.children[0].text;
        document.head.appendChild(el);
      });
    });
  }

  function propsFrom(json, given) {
    var props = {};
    var decl = {};
    try { decl = JSON.parse(json || '{}'); } catch (e) { decl = {}; }
    Object.keys(decl).forEach(function (k) {
      if (k.charAt(0) !== '$' && decl[k] && Object.prototype.hasOwnProperty.call(decl[k], 'default')) props[k] = decl[k].default;
    });
    return Object.assign(props, given || {});
  }

  function mount(container, parts, props) {
    applyHelmet(parts.template);
    var Component;
    try {
      Component = new Function('DCLogic', parts.code + '\n;return typeof Component === "function" ? Component : null;')(DCLogic);
    } catch (e) {
      return fail(container, 'The component’s script did not load: ' + e.message);
    }
    if (!Component) return fail(container, 'No class Component in this file.');
    var instance;
    var pending = false;
    var host = {
      schedule: function () {
        if (pending) return;
        pending = true;
        Promise.resolve().then(render);
      },
      setProps: function (p) {
        instance.props = Object.assign({}, instance.props, p);
        host.schedule();
      }
    };
    function render() {
      pending = false;
      var vals;
      try { vals = instance.renderVals() || {}; } catch (e) { return fail(container, 'renderVals() failed: ' + e.message); }
      var out = [];
      parts.template.children.forEach(function (c) { build(c, { vars: vals, parent: null }, '', false, out, host); });
      patchChildren(container, out, host);
    }
    try {
      instance = new Component(props);
    } catch (e) {
      return fail(container, 'The component did not start: ' + e.message);
    }
    instance.props = instance.props || props;
    instance.__host = host;
    render();
    if (typeof instance.componentDidMount === 'function') instance.componentDidMount();
    window.addEventListener('pagehide', function () {
      if (typeof instance.componentWillUnmount === 'function') instance.componentWillUnmount();
    });
    return host;
  }

  function fail(container, message) {
    container.textContent = '';
    var pre = document.createElement('pre');
    pre.style.cssText = 'margin: 24px; padding: 12px 14px; border: 1px solid #F2836F; border-radius: 6px; font: 12px ui-monospace, Menlo, monospace; white-space: pre-wrap';
    pre.textContent = message;
    container.appendChild(pre);
    return { schedule: function () {}, setProps: function () {} };
  }

  function mountImport(el, file, props, parentHost) {
    var proxy = { real: null, props: props, setProps: function (p) { proxy.props = p; if (proxy.real) proxy.real.setProps(p); } };
    fetch(file, { cache: 'no-cache' }).then(function (r) {
      if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
      return r.text();
    }).then(function (src) {
      var parts = fromSource(src);
      proxy.real = mount(el, parts, propsFrom(parts.props, proxy.props));
    }).catch(function () {
      fail(el, 'This board shows ' + file + ', which a page opened from disk cannot load. Serve this folder instead:\n\n  python3 -m http.server --directory lib/design/spec/html 8000\n\nthen open http://localhost:8000/' + location.pathname.split('/').pop() + ', or open ' + file + ' itself.');
    });
    return proxy;
  }

  function fromQuery() {
    var out = {};
    new URLSearchParams(location.search).forEach(function (v, k) {
      out[k] = v === 'true' ? true : v === 'false' ? false : v;
    });
    return out;
  }

  function start() {
    var x = document.querySelector('x-dc');
    if (!x) return;
    var container = document.createElement('div');
    container.style.display = 'contents';
    x.parentNode.insertBefore(container, x);
    var go = function (parts) {
      x.parentNode && x.parentNode.removeChild(x);
      mount(container, parts, propsFrom(parts.props, fromQuery()));
    };
    var fromDisk = location.protocol !== 'http:' && location.protocol !== 'https:';
    if (!fromDisk && !/[?&]source=dom\b/.test(location.search)) {
      fetch(location.href, { cache: 'no-cache' }).then(function (r) { return r.text(); }).then(function (src) { go(fromSource(src)); }).catch(function () { go(fromDom()); });
    } else {
      go(fromDom());
    }
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
