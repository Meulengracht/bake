<h1 align="center" style="margin-top: 0px;">Serve Daemon</h1>

The serve daemon is the build server backend. This needs to be implemented on an OS basis. This daemon does not neccessarily need to be implemented for each OS that supports chef applications, as it's an extra service. Running this daemon on a server, allows the use of the `--remote` switch for `bake`.

The daemon uses gracht protocols to communicate.

## Linux Features

N/A

## Windows Features

N/A

<h1 align="center" style="margin-top: 0px;">Cookd Protocol Specification</h1>

See protocols/cookd.gr file for the protocol definition.

```
service cookd (43) {
    func install(string packageName) : () = 1;
    func remove(string packageName) : (int result) = 2;
    func info(string packageName) : (package info) = 3;
    func listcount() : (uint count) = 4;
    func list() : (package[] packages) = 5;
    func update(string packageName) : () = 6;
    func update_all() : () = 7;
    
    event package_installed : (install_status status, package info) = 8;
    event package_removed : (package info) = 9;
    event package_updated : (update_status status, package info) = 10;
}
```

