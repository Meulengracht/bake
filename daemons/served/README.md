<h1 align="center" style="margin-top: 0px;">Serve Daemon</h1>

The serve daemon is the application backend. This needs to be implemented on an OS basis. This means if you seek to support chef applications, you need to implement serve for your OS. The serve daemon must adhere to the serve specification.

## Linux Features

Still needs to be implemented

## Windows Features

Still needs to be implemented

<h1 align="center" style="margin-top: 0px;">Serve Protocol Specification</h1>

See protocols/served.gr file for the protocol definition.

```
service served (42) {
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
