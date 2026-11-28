// SPDX-License-Identifier: AGPL-3.0-only
import generateGlsl from './generate-glsl.js'
import singlePassTransforms from './glsl/single-pass-transforms.js'
import { processGlsl, typeLookup, wrapGlslBody } from './types.js'

const expressionLimit = 64 * 1024
const identifier = /^[a-zA-Z][a-zA-Z_0-9]*$/
const parameterId = /^[a-z][a-z0-9_]{0,31}$/
const previousFrameName = 'voscPrevious'
const reserved = new Set([
  ...Object.getOwnPropertyNames(Object.prototype), 'parameter', 'setFunction', 'out', 'then',
  'time', 'resolution', 'main', 'st', previousFrameName,
])

function floatLiteral(value) {
  if (typeof value !== 'number') throw new Error('Single-pass transforms require finite float32 values.')
  const rounded = Math.fround(value)
  if (!Number.isFinite(rounded) || (rounded === 0 && value !== 0)) {
    throw new Error('Single-pass transforms require finite float32 values.')
  }
  const text = String(rounded)
  return /[.e]/.test(text) ? text : `${text}.0`
}

function compileTransform(definition) {
  const { returnType, args } = typeLookup[definition.type]
  return processGlsl({ ...definition, glslName: definition.name }, returnType, args)
}

// The caller evaluates source and owns the public patch surface. This API accepts
// synchronous calls on isolated chains and returns plain shader data.
export function compileSinglePass(build) {
  const transforms = singlePassTransforms()
  transforms.osc.glsl = `
    float phase = (_st.x + time * sync) * frequency;
    return vec4(0.5 + 0.5 * sin(vec3(phase) + vec3(-offset, 0.0, offset)), 1.0);
  `
  const chains = new WeakMap()
  const parameters = new Map()
  const operands = new WeakMap()
  const used = new Map()
  let output
  let invalidOutput = false

  class Chain {
    out(...args) {
      if (output || args.length) {
        invalidOutput = true
        throw new Error('Expected exactly one argument-free out().')
      }
      output = chains.get(this).slice()
    }
  }

  function register(transform) {
    if (transform.type === 'src') return transform
    Object.defineProperty(Chain.prototype, transform.name, {
      value(...args) {
        const chain = chains.get(this)
        if (transform.type === 'combine' || transform.type === 'combineCoord') {
          const branch = chains.get(args[0])
          if (!branch) throw new Error(`${transform.name} requires a single-pass branch.`)
          args[0] = { transforms: branch.slice() }
        }
        chain.push({ name: transform.name, transform, userArgs: args })
        return this
      },
    })
    return transform
  }

  const oscillator = register(compileTransform(transforms.osc))
  // The Editor emitter supplies the fixed sampling body at the worker boundary.
  const feedback = { name: previousFrameName, glslName: previousFrameName, type: 'src', inputs: [] }
  for (const definition of Object.values(transforms)) {
    if (definition.type !== 'src') register(compileTransform(definition))
  }

  function source(transform, userArgs) {
    const chain = new Chain()
    chains.set(chain, [{ name: transform.name, transform, userArgs }])
    return chain
  }
  const osc = (...args) => source(oscillator, args)
  function previousFrame(...args) {
    if (args.length) throw new Error('Previous frame takes no arguments.')
    return source(feedback, [])
  }

  function setFunction({ name, type, inputs, glsl }) {
    if (typeof name !== 'string' || !identifier.test(name) || name.startsWith('gl_') || name.startsWith('u_hydra_') ||
        reserved.has(name) || Object.hasOwn(Chain.prototype, name) || Object.hasOwn(transforms, name) ||
        type !== 'color' || !Array.isArray(inputs) || typeof glsl !== 'string') {
      throw new Error('Expected a single-pass color transform with a unique name.')
    }
    const inputNames = new Set()
    const validatedInputs = inputs.map(input => {
      if (typeof input.name !== 'string' || !identifier.test(input.name) || input.name.startsWith('gl_') ||
          reserved.has(input.name) || input.name.startsWith('u_hydra_') || inputNames.has(input.name) ||
          input.type !== 'float') {
        throw new Error('Expected unique float inputs for the single-pass color transform.')
      }
      inputNames.add(input.name)
      floatLiteral(input.default)
      return { name: input.name, type: 'float', default: input.default }
    })
    const { returnType, args } = typeLookup.color
    const signature = args.concat(validatedInputs.map(input => `float ${input.name}`)).join(', ')
    register({ name, type, inputs: validatedInputs, glslName: name, returnType,
      glsl: wrapGlslBody(`${returnType} ${name}(${signature})`, glsl) })
  }

  function parameter(id, value) {
    if (typeof id !== 'string' || !parameterId.test(id) || typeof value !== 'number' || value < 0 || value > 1) {
      throw new Error('Expected a single-pass parameter ID and default in [0, 1].')
    }
    floatLiteral(value)
    if (parameters.has(id) && parameters.get(id) !== value) throw new Error(`Conflicting defaults for param "${id}".`)
    parameters.set(id, value)
    const operand = Object.freeze(Object.create(null))
    operands.set(operand, { name: `u_hydra_${id}`, type: 'float', value: Math.fround(value) })
    return operand
  }

  const context = {
    generatePosition: false,
    checkExpression(expression) {
      if (expression.length > expressionLimit) throw new Error('Single-pass shader expression exceeds 64 KiB.')
    },
    isVertexSource: () => false,
    isShaderSource: source => Boolean(source?.transforms),
    formatArguments({ name, transform, userArgs }) {
      if (userArgs.length > transform.inputs.length) throw new Error(`Too many arguments to ${name}.`)
      return transform.inputs.map((input, index) => {
        const arg = userArgs[index]
        if (input.type === 'vec4') return { type: input.type, value: arg }
        const uniform = operands.get(arg)
        if (uniform && name === 'blend' && input.name === 'amount') {
          used.set(uniform.name, uniform)
          return { type: 'float', value: `clamp(${uniform.name}, 0.0, 1.0)` }
        }
        return { type: input.type, value: floatLiteral(arg === undefined ? input.default : arg) }
      })
    },
  }

  const result = build(Object.freeze({ osc, previousFrame, setFunction, parameter }))
  if (result?.then) throw new Error('Single-pass evaluation must be synchronous.')
  if (!output || invalidOutput) throw new Error('Expected exactly one argument-free out().')
  const shader = generateGlsl({ transforms: output }, context)
  return {
    colorExpression: shader.fragColor,
    readsPreviousFrame: shader.glslFunctions.some(({ transform }) => transform === feedback),
    functions: shader.glslFunctions.filter(({ transform }) => transform !== feedback)
      .map(({ name, transform }) => ({ name, source: transform.glsl })),
    uniforms: Array.from(used.values()).sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0),
  }
}
