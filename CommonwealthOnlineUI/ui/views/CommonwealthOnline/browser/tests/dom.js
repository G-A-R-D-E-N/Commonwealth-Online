"use strict";


var VOID_TAGS = new Set(["input", "br", "img", "hr", "meta", "link"]);

function findTagEnd(html, start) {
  var quote = null;
  for (var i = start + 1; i < html.length; i++) {
    var ch = html[i];
    if (quote) {
      if (ch === quote) quote = null;
    } else if (ch === '"' || ch === "'") {
      quote = ch;
    } else if (ch === ">") {
      return i;
    }
  }
  return -1;
}

function parseAttributes(text) {
  var attributes = {};
  var re = /([a-zA-Z_:][\w:.-]*)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))?/g;
  var match;
  while ((match = re.exec(text)) !== null) {
    var value = match[2] !== undefined ? match[2] : match[3] !== undefined ? match[3] : match[4];
    attributes[match[1].toLowerCase()] = value === undefined ? "" : value;
  }
  return attributes;
}

function matchesSimple(el, sel) {
  if (!sel) return false;
  if (sel.charAt(0) === "#") return el.id === sel.slice(1);
  if (sel.charAt(0) === ".") return (" " + el.className + " ").indexOf(" " + sel.slice(1) + " ") >= 0;
  if (sel.charAt(0) === "[") {
    var m = /^\[([\w-]+)(?:=["']?([^"'\]]*)["']?)?\]$/.exec(sel);
    if (!m) return false;
    var value = el.getAttribute(m[1]);
    return m[2] === undefined ? value !== undefined && value !== null : String(value) === m[2];
  }
  return el.tagName === sel.toUpperCase();
}

function tokenizeCompound(sel) {
  var tokens = [];
  var re = /(\[[^\]]*\]|[.#][\w-]+|[a-zA-Z][\w-]*)/g;
  var match;
  while ((match = re.exec(sel)) !== null) tokens.push(match[1]);
  return tokens;
}

function matches(el, selector) {
  var groups = String(selector).split(",");
  for (var i = 0; i < groups.length; i++) {
    var group = groups[i].trim();
    if (!group) continue;
    var tokens = tokenizeCompound(group);
    if (!tokens.length) continue;
    var ok = true;
    for (var j = 0; j < tokens.length; j++) {
      if (!matchesSimple(el, tokens[j])) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

function descendants(el, out) {
  for (var i = 0; i < el.children.length; i++) {
    out.push(el.children[i]);
    descendants(el.children[i], out);
  }
  return out;
}

function emitEvent(node, type, relatedTarget) {
  var event = { type: type, target: node, relatedTarget: relatedTarget || null };
  var bubbles = type === "focusin" || type === "focusout";
  var current = node;
  while (current) {
    var list = current._listeners ? current._listeners[type] : null;
    if (list) {
      for (var i = 0; i < list.length; i++) list[i](event);
    }
    current = bubbles ? current.parentNode : null;
  }
}

function createElement(tagName) {
  var el = {
    tagName: String(tagName || "div").toUpperCase(),
    id: "",
    className: "",
    type: "",
    value: "",
    textContent: "",
    children: [],
    parentNode: null,
    isContentEditable: false,
    style: {},
    _attributes: {},
    _listeners: {},
    dataset: {}
  };

  el.classList = {
    add: function (name) {
      if (!el.classList.contains(name)) el.className = (el.className + " " + name).trim();
    },
    remove: function (name) {
      el.className = (" " + el.className + " ").split(" " + name + " ").join(" ").trim();
    },
    toggle: function (name, force) {
      var on = force === undefined ? !el.classList.contains(name) : !!force;
      if (on) el.classList.add(name);
      else el.classList.remove(name);
    },
    contains: function (name) {
      return (" " + el.className + " ").indexOf(" " + name + " ") >= 0;
    }
  };

  el.addEventListener = function (type, handler) {
    (el._listeners[type] = el._listeners[type] || []).push(handler);
  };
  el.appendChild = function (child) {
    child.parentNode = el;
    child.ownerDocument = el.ownerDocument;
    el.children.push(child);
    return child;
  };
  el.removeChild = function (child) {
    var index = el.children.indexOf(child);
    if (index >= 0) el.children.splice(index, 1);
    child.parentNode = null;
    return child;
  };
  el.dispatchEvent = function (event) {
    if (!event) return;
    var target = event.target;
    if (target === undefined || target === null) {
      try {
        event.target = el;
      } catch (e) {
        Object.defineProperty(event, "target", { value: el, configurable: true });
      }
    }
    var current = el;
    while (current) {
      var list = current._listeners ? current._listeners[event.type] : null;
      if (list) {
        for (var i = 0; i < list.length; i++) list[i](event);
      }
      current = event.bubbles ? current.parentNode : null;
    }
  };
  el.focus = function () {
    var doc = el.ownerDocument;
    if (!doc || doc.activeElement === el) return;
    var previous = doc.activeElement;
    doc.activeElement = el;
    if (previous && previous !== doc.body && previous !== el) {
      emitEvent(previous, "focusout", el);
      emitEvent(previous, "blur", el);
    }
    emitEvent(el, "focus", previous);
    emitEvent(el, "focusin", previous);
  };
  el.blur = function () {
    var doc = el.ownerDocument;
    if (!doc || doc.activeElement !== el) return;
    doc.activeElement = doc.body;
    emitEvent(el, "focusout", null);
    emitEvent(el, "blur", null);
  };
  el.click = function () {
    el.dispatchEvent({ type: "click" });
  };
  el.contains = function (node) {
    if (node === el) return true;
    for (var i = 0; i < el.children.length; i++) {
      if (el.children[i].contains(node)) return true;
    }
    return false;
  };
  el.closest = function (selector) {
    var node = el;
    while (node) {
      if (node.tagName && matches(node, selector)) return node;
      node = node.parentNode;
    }
    return null;
  };
  el.querySelectorAll = function (selector) {
    return descendants(el, []).filter(function (node) {
      return matches(node, selector);
    });
  };
  el.querySelector = function (selector) {
    return el.querySelectorAll(selector)[0] || null;
  };
  el.setAttribute = function (name, value) {
    el._attributes[name] = String(value);
    if (name === "id") el.id = String(value);
    if (name === "class") el.className = String(value);
    if (name === "value") el.value = String(value);
    if (name === "type") el.type = String(value);
    var dataMatch = /^data-([a-z0-9-]+)$/i.exec(name);
    if (dataMatch) {
      var camel = dataMatch[1].replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); });
      el.dataset[camel] = String(value);
    }
  };
  el.getAttribute = function (name) {
    if (name === "id") return el.id;
    if (name === "class") return el.className;
    return Object.prototype.hasOwnProperty.call(el._attributes, name) ? el._attributes[name] : null;
  };

  var inner = "";
  Object.defineProperty(el, "innerHTML", {
    get: function () {
      return inner;
    },
    set: function (html) {
      inner = String(html);
      el.children = parseHtml(inner, el);
    }
  });

  return el;
}

function parseHtml(html, parent) {
  var topLevel = [];
  var stack = [parent];
  var i = 0;
  while (i < html.length) {
    var lt = html.indexOf("<", i);
    if (lt < 0) break;
    var gt = findTagEnd(html, lt);
    if (gt < 0) break;
    var tag = html.slice(lt, gt + 1);
    i = gt + 1;

    var m = /^<\s*(\/?)\s*([a-zA-Z][\w-]*)([\s\S]*?)(\/?)\s*>$/.exec(tag);
    if (!m) continue;
    if (m[1] === "/") {
      if (stack.length > 1) stack.pop();
      continue;
    }

    var node = createElement(m[2]);
    node.ownerDocument = parent.ownerDocument;
    var attributes = parseAttributes(m[3]);
    Object.keys(attributes).forEach(function (name) {
      node.setAttribute(name, attributes[name]);
    });
    var target = stack[stack.length - 1];
    target.children.push(node);
    node.parentNode = target;
    if (target === parent) topLevel.push(node);

    var selfClosing = m[4] === "/" || VOID_TAGS.has(node.tagName.toLowerCase());
    if (!selfClosing) stack.push(node);
  }
  return topLevel;
}

function createEnvironment(makeWindow) {
  var document = {
    body: createElement("body"),
    activeElement: null,
    _listeners: {},
    _point: null,
    addEventListener: function (type, handler) {
      (document._listeners[type] = document._listeners[type] || []).push(handler);
    },
    dispatchEvent: function (event) {
      event.target = event.target || document;
      var list = document._listeners[event.type] || [];
      for (var i = 0; i < list.length; i++) list[i](event);
    },
    createElement: createElement,
    getElementById: function (id) {
      return document.body.querySelector("#" + id);
    },
    querySelector: function (selector) {
      return document.body.querySelector(selector);
    },
    querySelectorAll: function (selector) {
      return document.body.querySelectorAll(selector);
    },
    elementFromPoint: function (x, y) {
      return typeof document._point === "function" ? document._point(x, y) : null;
    }
  };
  document.body.ownerDocument = document;
  document.activeElement = document.body;

  var storage = {};
  var window = makeWindow ? makeWindow() : {};
  window.document = document;
  window.console = console;
  window.localStorage = {
    getItem: function (key) {
      return Object.prototype.hasOwnProperty.call(storage, key) ? storage[key] : null;
    },
    setItem: function (key, value) {
      storage[key] = String(value);
    },
    removeItem: function (key) {
      delete storage[key];
    }
  };
  window.setTimeout = function (handler) {
    window.__timers.push(handler);
    return window.__timers.length;
  };
  window.clearTimeout = function () {};
  window.__timers = [];
  window.__flushTimers = function () {
    var pending = window.__timers;
    window.__timers = [];
    pending.forEach(function (handler) {
      handler();
    });
  };
  window.requestAnimationFrame = function () {
    return 0;
  };
  window._listeners = {};
  window.addEventListener = function (type, handler) {
    (window._listeners[type] = window._listeners[type] || []).push(handler);
  };
  window.dispatchEvent = function (event) {
    event.target = event.target || window;
    var list = window._listeners[event.type] || [];
    for (var i = 0; i < list.length; i++) list[i](event);
  };
  window.window = window;

  return { document: document, window: window, createElement: createElement };
}

module.exports = {
  createElement: createElement,
  createEnvironment: createEnvironment,
  parseHtml: parseHtml,
  matches: matches
};
