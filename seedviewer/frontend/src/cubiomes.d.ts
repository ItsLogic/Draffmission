declare module '/cubiomes.js' {
  interface ModuleOptions {
    locateFile?: (path: string) => string
  }
  const factory: (options?: ModuleOptions) => Promise<any>
  export default factory
}
