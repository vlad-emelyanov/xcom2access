// Load the SDK generator into the running game. DllMain only spawns a worker thread, so this
// never touches the game thread or ProcessEvent — unlike outbound script calls.
// Forward slashes on purpose: LoadLibrary accepts them, and they survive shell quoting.
rpc.exports = {
    main() {
        const path = 'E:/Games/modding/XCOM2/deps/CodeRed-Generator/x64/Release/CodeRedGenerator.dll';
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
