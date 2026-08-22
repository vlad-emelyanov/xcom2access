// Load the SDK generator into the running game. DllMain only spawns a worker thread, so this
// never touches the game thread or ProcessEvent — unlike outbound script calls.
// Forward slashes on purpose: LoadLibrary accepts them, and they survive shell quoting.
rpc.exports = {
    // The DLL path is an ARGUMENT, not a constant: this repo has lived at E:/Games/modding/XCOM2
    // and at C:/modding/xcom2access, and a path baked in here is wrong on every machine but the
    // one that last edited it. `just sdk-gen` passes {{ gen_dll }}, which is derived from
    // justfile_directory(). The fallback keeps a bare `frida-run.py --script ...` working from
    // the repo root.
    main(dllPath) {
        const path = dllPath || 'deps/CodeRed-Generator/x64/Release/CodeRedGenerator.dll';
        const found = () => Process.enumerateModules()
            .filter(m => m.name.toLowerCase().indexOf('codered') >= 0)
            .map(m => m.name + ' @' + m.base);
        if (found().length) return { alreadyLoaded: found() };
        try {
            Module.load(path);
        } catch (e) {
            return { error: e.message, path: path };
        }
        return { loaded: found(), note: 'generation starts 10s after load; writes generated_sdk_new/' };
    },
};
