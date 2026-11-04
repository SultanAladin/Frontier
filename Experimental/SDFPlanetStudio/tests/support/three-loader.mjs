/**
 * Module resolve hook: point `three` at the headless mock so app modules can be
 * imported and executed on a machine with no WebGL.
 */
export async function resolve(specifier, context, nextResolve) {
  if (specifier === 'three') {
    return {
      url: new URL('./three-mock.mjs', import.meta.url).href,
      shortCircuit: true,
    };
  }
  return nextResolve(specifier, context);
}
