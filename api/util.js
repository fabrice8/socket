import { IllegalConstructorError } from './errors.js'
import { MIMEParams } from './mime/params.js'
import { MIMEType } from './mime/type.js'
import { Buffer } from './buffer.js'
import { URL } from './url.js'
import types from './util/types.js'

import * as exports from './util.js'

export { types }

const TypedArrayPrototype = Object.getPrototypeOf(Uint8Array.prototype)
const ObjectPrototype = Object.prototype

const kIgnoreInspect = inspect.ignore = Symbol.for('socket.runtime.util.inspect.ignore')

function maybeURL (...args) {
  try {
    return new URL(...args)
  } catch (_) {
    return null
  }
}

export const TextDecoder = globalThis.TextDecoder
export const TextEncoder = globalThis.TextEncoder
export const isArray = Array.isArray.bind(Array)

export const inspectSymbols = [
  Symbol.for('socket.runtime.util.inspect.custom'),
  Symbol.for('nodejs.util.inspect.custom')
]

inspect.custom = inspectSymbols[0]

export function debug (section) {
  let enabled = false
  const env = globalThis.__args?.env ?? {}
  const sections = [].concat(
    (env.SOCKET_DEBUG ?? '').split(','),
    (env.NODE_DEBUG ?? '').split(',')
  ).map((section) => section.trim())

  if (section && sections.includes(section)) {
    enabled = true
  }

  function logger (...args) {
    if (enabled) {
      return console.debug(...args)
    }
  }

  Object.defineProperty(logger, 'enabled', {
    configurable: false,
    enumerable: false,
    get: () => enabled,
    set: (value) => {
      if (value === true) {
        enabled = true
      } else if (value === false) {
        enabled = false
      }
    }
  })

  return logger
}

export function hasOwnProperty (object, property) {
  return ObjectPrototype.hasOwnProperty.call(object, String(property))
}

export function isDate (object) {
  return types.isDate(object)
}

export function isTypedArray (object) {
  return types.isTypedArray(object)
}

export function isArrayLike (input) {
  return (
    (Array.isArray(input) || isTypedArray(input)) &&
    input !== TypedArrayPrototype &&
    input !== Buffer.prototype
  )
}

export function isError (object) {
  return types.isNativeError(object) || object instanceof globalThis.Error
}

export function isSymbol (value) {
  return typeof value === 'symbol'
}

export function isNumber (value) {
  return !isUndefined(value) && !isNull(value) && (
    typeof value === 'number' ||
    value instanceof Number
  )
}

export function isBoolean (value) {
  return !isUndefined(value) && !isNull(value) && (
    value === true ||
    value === false ||
    value instanceof Boolean
  )
}

export function isArrayBufferView (buf) {
  return !Buffer.isBuffer(buf) && ArrayBuffer.isView(buf)
}

export function isAsyncFunction (object) {
  return types.isAsyncFunction(object)
}

export function isArgumentsObject (object) {
  return types.isArgumentsObject(object)
}

export function isEmptyObject (object) {
  return (
    object !== null &&
    typeof object === 'object' &&
    Object.keys(object).length === 0
  )
}

export function isObject (object) {
  return (
    object !== null &&
    typeof object === 'object'
  )
}

export function isUndefined (value) {
  return value === undefined
}

export function isNull (value) {
  return value === null
}

export function isNullOrUndefined (value) {
  return isNull(value) || isUndefined(value)
}

export function isPrimitive (value) {
  return (
    isNullOrUndefined(value) ||
    typeof value === 'number' ||
    typeof value === 'string' ||
    typeof value === 'symbol' ||
    typeof value === 'boolean'
  )
}

export function isRegExp (value) {
  return value && value instanceof RegExp
}

export function isPlainObject (object) {
  return types.isPlainObject(object)
}

export function isArrayBuffer (object) {
  return object !== null && object instanceof ArrayBuffer
}

export function isBufferLike (object) {
  return isArrayBuffer(object) || isTypedArray(object) || Buffer.isBuffer(object)
}

export function isFunction (value) {
  return (
    typeof value === 'function' &&
    typeof value.toString === 'function' &&
    !/^class/.test(value.toString())
  )
}

export function isErrorLike (error) {
  if (error instanceof Error) return true
  return isObject(error) && 'name' in error && 'message' in error
}

export function isClass (value) {
  return (
    typeof value === 'function' &&
    value.prototype.constructor !== Function
  )
}

export function isBuffer (value) {
  return Buffer.isBuffer(value)
}

export function isPromiseLike (object) {
  return isFunction(object?.then)
}

export function toString (object) {
  return Object.prototype.toString.call(object)
}

export function toBuffer (object, encoding = undefined) {
  if (Buffer.isBuffer(object)) {
    return object
  } else if (isTypedArray(object)) {
    return Buffer.from(object.buffer)
  } else if (typeof object?.toBuffer === 'function') {
    return toBuffer(object.toBuffer(), encoding)
  }

  return Buffer.from(object, encoding)
}

export function toProperCase (string) {
  if (!string) return ''
  return string[0].toUpperCase() + string.slice(1)
}

export function splitBuffer (buffer, highWaterMark) {
  const buffers = []

  buffer = Buffer.from(buffer)

  do {
    const pointer = buffer.subarray(0, highWaterMark)
    const value = Buffer.alloc(pointer.byteLength)
    value.set(pointer)
    buffers.push(value)
    buffer = buffer.subarray(highWaterMark)
  } while (buffer.length > highWaterMark)

  if (buffer.length) {
    buffers.push(buffer)
  }

  return buffers
}

export function clamp (value, min, max) {
  if (!Number.isFinite(value)) {
    value = min
  }

  return Math.min(max, Math.max(min, value))
}

Object.defineProperties(promisify, {
  custom: {
    configurable: false,
    enumerable: false,
    value: Symbol.for('nodejs.util.promisify.custom')
  },
  args: {
    configurable: false,
    enumerable: false,
    value: Symbol.for('nodejs.util.promisify.args')
  }
})

export function promisify (original) {
  if (original && typeof original === 'object') {
    let object = Object.create(null)

    if (
      // @ts-ignore
      original[promisify.custom] &&
      // @ts-ignore
      typeof original[promisify.custom] === 'object'
    ) {
      // @ts-ignore
      object = original[promisify.custom]
    } else if (original.promises && typeof original.promises === 'object') {
      object = original.promises
    }

    for (const key in original) {
      const value = original[key]
      if (typeof value === 'function' || (value && typeof value === 'object')) {
        object[key] = promisify(original[key].bind(original))
      } else {
        object[key] = original[key]
      }
    }

    // @ts-ignore
    Object.defineProperty(object, promisify.custom, {
      configurable: true,
      enumerable: false,
      writable: false,
      // @ts-ignore
      __proto__: null,
      value: object
    })

    return object
  }

  if (typeof original !== 'function') {
    throw new TypeError('Expecting original to be a function or object.')
  }

  // @ts-ignore
  if (original[promisify.custom]) {
    // @ts-ignore
    const fn = original[promisify.custom]
    // @ts-ignore
    Object.defineProperty(fn, promisify.custom, {
      configurable: true,
      enumerable: false,
      writable: false,
      // @ts-ignore
      __proto__: null,
      value: fn
    })

    return fn
  }

  // @ts-ignore
  const argumentNames = Array.isArray(original[promisify.args])
  // @ts-ignore
    ? original[promisify.args]
    : []

  async function fn (...args) {
    return await new Promise((resolve, reject) => {
      return Reflect.apply(original, this, args.concat(callback))
      function callback (err, ...values) {
        let [result] = values

        if (err) {
          return reject(err)
        }

        if (argumentNames.length) {
          result = {}

          for (let i = 0; i < argumentNames.length; ++i) {
            result[argumentNames[i]] = values[i]
          }
        }

        return resolve(result)
      }
    })
  }

  Object.setPrototypeOf(fn, Object.getPrototypeOf(original))

  return fn
}

export function inspect (value, options) {
  const ctx = {
    seen: options?.seen || [],
    depth: typeof options?.depth !== 'undefined' ? options.depth : 2,
    showHidden: options?.showHidden || false,
    customInspect: (
      options?.customInspect === undefined
        ? true
        : options.customInspect
    ),

    ...options,
    options: {
      stylize (label, style) {
        return label
      },
      ...options
    }
  }

  return formatValue(ctx, value, ctx.depth)

  function formatValue (ctx, value, depth) {
    if (value instanceof Symbol || typeof value === 'symbol') {
      return String(value)
    }

    // nodejs `value.inspect()` parity
    if (
      ctx.customInspect &&
      !(value?.constructor && value?.constructor?.prototype === value)
    ) {
      if (
        isFunction(value?.inspect) &&
        value?.inspect !== inspect &&
        value !== globalThis &&
        value !== globalThis?.system &&
        value !== globalThis?.__args &&
        value?.inspect[kIgnoreInspect] !== true
      ) {
        const formatted = value.inspect(depth, ctx)

        if (typeof formatted !== 'string') {
          return formatValue(ctx, formatted, depth)
        }

        return formatted
      } else if (value) {
        for (const inspectSymbol of inspectSymbols) {
          if (isFunction(value[inspectSymbol]) && value[inspectSymbol] !== inspect) {
            const formatted = value[inspectSymbol](
              depth,
              ctx.options,
              inspect
            )

            if (typeof formatted !== 'string') {
              return formatValue(ctx, formatted, depth)
            }

            return formatted
          }
        }
      }
    }

    if (value === undefined) {
      return 'undefined'
    }

    if (value === null) {
      return 'null'
    }

    if (typeof value === 'string') {
      const formatted = JSON.stringify(value)
        .replace(/^"|"$/g, '')
        .replace(/'/g, "\\'")
        .replace(/\\"/g, '"')

      return `'${formatted}'`
    }

    if (typeof value === 'number' || typeof value === 'boolean') {
      return String(value)
    }

    if (typeof value === 'bigint') {
      return String(value) + 'n'
    }

    if (value instanceof WeakSet) {
      return 'WeakSet { <items unknown> }'
    }

    if (value instanceof WeakMap) {
      return 'WeakMap { <items unknown> }'
    }

    let typename = ''

    const braces = ['{', '}']
    const isArrayLikeValue = isArrayLike(value)

    try {
      if (value instanceof MIMEParams) {
        braces[0] = `MIMEParams(${value.size}) ${braces[0]}`
      } else if (value instanceof Map) {
        braces[0] = `Map(${value.size}) ${braces[0]}`
      } else if (value instanceof Set) {
        braces[0] = `Set(${value.size}) ${braces[0]}`
      }
    } catch {
      braces.splice(0, braces.length)
    }

    let keys = []
    try {
      keys = value instanceof Map
        ? Array.from(value.keys())
        : new Set(Object.keys(value))
    } catch {}

    const enumerableKeys = value instanceof Set
      ? Array(value.size).fill(0).map((_, i) => i)
      : Object.fromEntries(Array.from(keys).map((k) => [k, true]))

    if (ctx.showHidden) {
      try {
        const hidden = Object.getOwnPropertyNames(value)
        for (const key of hidden) {
          if (value instanceof Error && !/stack|message|name/.test(key)) {
            keys.add(key)
          }
        }
      } catch (err) {}
    }

    if (isArrayLikeValue) {
      braces[0] = '['
      braces[1] = ']'
    }

    if (isAsyncFunction(value)) {
      const name = value.name ? `: ${value.name}` : ''
      typename = `[AsyncFunction${name}]`
    } else if (isFunction(value)) {
      const name = value.name ? `: ${value.name}` : ''
      typename = `[Function${name}]`
    }

    if (value instanceof RegExp) {
      typename = `${RegExp.prototype.toString.call(value)}`
    }

    if (value instanceof Date) {
      typename = `${Date.prototype.toString.call(value)}`
      braces[0] = '['
      braces[1] = ']'
    }

    if (value instanceof Error) {
      typename = `${Error.prototype.toString.call(value)}`
      braces[0] = ''
      braces[1] = ''

      if (value.cause) {
        keys.add('cause')
      }

      if (value.code) {
        enumerableKeys.code = true
        keys.add('code')
      }
    }

    if (isArgumentsObject(value)) {
      typename = 'Arguments'
      braces[0] = '{'
      braces[1] = '}'
    } else if (types.isSetIterator(value)) {
      typename = 'Set Iterator'
    } else if (types.isMapIterator(value)) {
      typename = 'Map Iterator'
    } else if (types.isIterator(value)) {
      typename = 'Iterator'
    } else if (types.isAsyncIterator(value)) {
      typename = 'AsyncIterator'
    } else if (types.isGeneratorFunction(value)) {
      typename = 'GeneratorFunction'
    } else if (types.isGeneratorObject(value)) {
      typename = 'Generator'
    } else if (types.isAsyncGeneratorFunction(value)) {
      typename = 'AsyncGeneratorFunction'
    }

    if (!(value instanceof Map || value instanceof Set)) {
      if (
        typeof value === 'object' &&
        typeof value?.constructor === 'function' &&
        (value.constructor !== Object && value.constructor !== Array)
      ) {
        let tag = value?.[Symbol.toStringTag] || value.constructor.name || value?.toString

        if (typeof tag === 'function') {
          tag = tag.call(value)
        }

        if (tag === '[object Object]') {
          tag = ''
        }

        braces[0] = `${tag} ${braces[0]}`
      }

      if (keys.size === 0 && !(value instanceof Error)) {
        if (isFunction(value)) {
          return typename
        } else if (!isArrayLikeValue || value.length === 0) {
          return `${braces[0]}${typename}${braces[1]}`
        } else if (!isArrayLikeValue) {
          return typename
        }
      }
    }

    if (depth < 0) {
      if (value instanceof RegExp) {
        return RegExp.prototype.toString.call(value)
      }

      return '[Object]'
    }

    ctx.seen.push(value)

    const output = []

    if (!isArgumentsObject(value) && (isArrayLikeValue || value instanceof Set)) {
      // const items = isArrayLikeValue ? value : Array.from(value.values())
      const size = isArrayLikeValue ? value.length : value.size
      for (let i = 0; i < size; ++i) {
        const key = String(i)
        if (value instanceof Set || hasOwnProperty(value, key)) {
          if (key === 'length' && Array.isArray(value)) {
            continue
          }
          output.push(formatProperty(
            ctx,
            value,
            depth,
            enumerableKeys,
            key,
            true
          ))
        }
      }

      for (const key of keys) {
        if (!/^\d+$/.test(key) && key !== 'length') {
          output.push(formatProperty(
            ctx,
            value,
            depth,
            enumerableKeys,
            key,
            true
          ))
        }
      }
    } else if (typeof value === 'function') {
      for (const key of keys) {
        if (
          !/^\d+$/.test(key) &&
          key !== 'name' &&
          key !== 'length' &&
          key !== 'prototype' &&
          key !== 'constructor'
        ) {
          output.push(formatProperty(
            ctx,
            value,
            depth,
            enumerableKeys,
            key,
            false
          ))
        }
      }
    } else {
      output.push(...Array.from(keys).map((key) => formatProperty(
        ctx,
        value,
        depth,
        enumerableKeys,
        key,
        false
      )))
    }

    ctx.seen.pop()

    if (value instanceof Error) {
      let out = ''

      if (value?.message && !value?.stack?.startsWith?.(`${value?.name}: ${value?.message}`)) {
        out += `${value.name}: ${value.message}\n`
      }

      const formatWebkitErrorStackLine = (line) => {
        const [symbol = '', location = ''] = line.endsWith('@')
          ? [line.slice(0, -1)]
          : line.startsWith('@')
            ? ['', line.slice(1)]
            : line.split('@')

        let output = []
        const root = new URL('../', import.meta.url || globalThis.location.href).pathname

        let [context, lineno, colno] = (
          maybeURL(location)?.pathname?.split(/:/) ||
          location?.split(/:/) ||
          []
        )

        if (symbol) {
          output.push(symbol)
        }

        if (context) {
          context = context.replace(root, '')
          if (/socket\//.test(context)) {
            context = context.replace('socket/', 'socket:').replace(/.js$/, '')
          }
        }

        if (context && lineno && colno) {
          output.push(`(${context}:${lineno}:${colno})`)
        } else if (context && lineno) {
          output.push(`(${context}:${lineno})`)
        } else if (context) {
          output.push(`${context}`)
        } else if (!symbol) {
          output.push('<anonymous>')
        }

        output = output.map((entry) => entry.trim()).filter(Boolean)

        if (output.length) {
          output.unshift('    at')
        }

        return output.filter(Boolean).join(' ')
      }

      out += (typeof value?.stack === 'string' ? value.stack : '')
        .split('\n')
        .map((line) => line.includes(`${value.name}: ${value.message}`) || /^\s*at\s/.test(line)
          ? line
          : formatWebkitErrorStackLine(line)
        )
        .filter(Boolean)
        .join('\n')

      if (keys.size) {
        out += ' {\n'
      }

      out += `  ${output.join(',\n  ')}`
      if (keys.size) {
        out += '\n}'
      }

      return out.trim()
    }

    const length = output.reduce((p, c) => (p + c.length + 1), 0)

    if (Object.getPrototypeOf(value) === null) {
      let tag = value?.[Symbol.toStringTag] || value?.toString

      if (typeof tag === 'function') {
        tag = tag.call(value)
      }

      braces[0] = `${tag || '[Object: null prototype]'} ${braces[0]}`
    }

    if (length > 80) {
      return `${braces[0]}\n${!typename ? '' : ` ${typename}\n`}  ${output.join(',\n  ')}\n${braces[1]}`
    }

    return `${braces[0]}${typename}${output.length ? ` ${output.join(', ')} ` : ''}${braces[1]}`
  }

  function formatProperty (
    ctx,
    value,
    depth,
    enumerableKeys,
    key,
    isArrayLikeValue
  ) {
    const descriptor = { value: undefined }
    const output = ['', '']

    try {
      descriptor.value = value[key]
    } catch (err) {}

    try {
      Object.assign(descriptor, Object.getOwnPropertyDescriptor(value, key))
    } catch (err) {}

    if (descriptor.get && descriptor.set) {
      output[1] = '[Getter/Setter]'
    } else if (descriptor.get) {
      output[1] = '[Getter]'
    } else if (descriptor.set) {
      output[1] = '[Setter]'
    }

    if (!hasOwnProperty(enumerableKeys, key)) {
      output[0] = `[${key}]`
    }

    if (!output[1]) {
      if (ctx.seen.includes(descriptor.value)) {
        output[1] = '[Circular]'
      } else {
        const tmp = value instanceof Set
          ? Array.from(value.values())[key]
          : value instanceof Map
            ? value.get(key)
            : descriptor.value

        if (depth === null) {
          output[1] = formatValue(ctx, tmp, null)
        } else {
          output[1] = formatValue(ctx, tmp, depth - 1)
        }

        if (output[1].includes('\n')) {
          if (isArrayLikeValue) {
            output[1] = output[1]
              .split('\n')
              .map((line) => `  ${line}`)
              .join('\n')
              .slice(2)
          } else {
            output[1] = '\n' + output[1]
              .split('\n')
              .map((line) => `    ${line}`)
              .join('\n')
          }
        }
      }
    }

    if (!output[0]) {
      if (isArrayLikeValue && /^\d+$/.test(key)) {
        return output[1]
      }

      output[0] = JSON.stringify(String(key))
        .replace(/^"/, '')
        .replace(/"$/, '')
        .replace(/'/g, "\\'")
        .replace(/\\"/g, '"')
        .replace(/(^"|"$)/g, "'")
    }

    if (value instanceof Map) {
      return output.join(' => ')
    } else {
      return output.join(': ')
    }
  }
}

export function format (format, ...args) {
  let options = args.pop()

  if (!options || typeof options !== 'object' || !options?.seen || !options?.depth) {
    if (options !== undefined) {
      args.push(options)
    }
    options = undefined
  }

  if (typeof format !== 'string') {
    return [format]
      .concat(args)
      .map((arg) => inspect(arg, { ...options }))
      .join(' ')
  }

  const regex = /%[dfijloOsuxz%]/ig

  let i = 0
  let str = format.replace(regex, (x) => {
    if (x === '%%') {
      return '%'
    }

    if (i >= args.length) {
      return x
    }

    if (args[i] === globalThis) {
      i++
    }

    if (args[i] === globalThis?.system) {
      i++
      return '[System]'
    }

    switch (x) {
      case '%d': return Number(args[i++])
      case '%u': return Number(args[i++])
      case '%l': return Number(args[i++])
      case '%lu': return Number(args[i++])
      case '%llu': return Number(args[i++])
      case '%zu': return Number(args[i++])
      case '%f': return parseFloat(args[i++])
      case '%i': return parseInt(args[i++])
      case '%o': return inspect(args[i++], { showHidden: true })
      case '%O': return inspect(args[i++], {})
      case '%j':
        try {
          return JSON.stringify(args[i++])
        } catch (_) {
          return '[Circular]'
        }

      case '%J':
        try {
          return JSON.stringify(args[i++], null, ' ')
        } catch (_) {
          return '[Circular]'
        }

      case '%s': return String(args[i++])
      case '%ls': return String(args[i++])
      case '%S': return String(args[i++])
      case '%LS': return String(args[i++])
    }

    return x
  })

  for (const arg of args.slice(i)) {
    if (arg === null || typeof arg !== 'object') {
      str += ' ' + arg
    } else {
      str += ' ' + inspect(arg, { ...options })
    }
  }

  return str
}

export function parseJSON (string) {
  if (string !== null) {
    string = String(string)

    try {
      const encoded = encodeURIComponent(string)
      // detect back slashes without regex hell as they may not be escaped
      // ie: '\Users' instead of '\\Users' which results in invalid JSON
      if (encoded.includes('%5C')) {
        // use `RegExp` because a literal regex may throw a syntax error
        // in environments where negative lookbehinds are not supported
        // see https://stackoverflow.com/a/50434875 for platform support
        // eslint-disable-next-line prefer-regex-literals
        const regex = new RegExp('(?<!%5C)%5C', 'g')
        return JSON.parse(decodeURIComponent(encoded.replace(regex, '%5C%5C')))
      }
    } catch (err) {}

    try {
      return JSON.parse(string)
    } catch (err) {}
  }

  return null
}

export function parseHeaders (headers) {
  if (Array.isArray(headers)) {
    headers = headers.map((h) => h.trim()).join('\n')
  }

  if (typeof headers !== 'string') {
    return []
  }

  return headers
    .split(/\r?\n/)
    .map((l) => l.trim().split(':'))
    .filter((e) => e.length >= 2)
    .map((e) => [e[0].trim().toLowerCase(), e.slice(1).join(':').trim().toLowerCase()])
    .filter((e) => e[0].length && e[1].length)
}

export function noop () {}

export class IllegalConstructor {
  constructor () {
    throw new IllegalConstructorError()
  }
}

const percentageRegex = /^(100(\.0+)?|[1-9]?\d(\.\d+)?)%$/

export function isValidPercentageValue (input) {
  return percentageRegex.test(input)
}

export function compareBuffers (a, b) {
  return toBuffer(a).compare(toBuffer(b))
}

export function inherits (Constructor, Super) {
  Object.defineProperty(Constructor, 'super_', {
    configurable: true,
    writable: true,
    value: Super,
    __proto__: null
  })

  Object.setPrototypeOf(Constructor.prototype, Super.prototype)
}

export const ESM_TEST_REGEX = /\b(import\s*[\w{},*\s]*\s*from\s*['"][^'"]+['"]|export\s+(?:\*\s*from\s*['"][^'"]+['"]|default\s*from\s*['"][^'"]+['"]|[\w{}*\s,]+))\s*(?:;|\b)/

/**
 * @ignore
 * @param {string} source
 * @return {boolean}
 */
export function isESMSource (source) {
  if (ESM_TEST_REGEX.test(source)) {
    return true
  }

  return false
}

export function deprecate (...args) {
  // noop
}

export { MIMEType, MIMEParams }
export default exports
