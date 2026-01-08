<h1 align="center" style="margin-top: 0px;">Serve Daemon</h1>

The serve daemon is the application backend for Chef package management. It manages the installation, removal, updating, and lifecycle of Chef packages on the system. The daemon must be implemented on an OS basis and adhere to the serve protocol specification.

## Architecture Overview

### Core Components

- **State Layer** (`state/state.c`): Pure data persistence layer handling SQLite database operations with deferred write pattern for transactional integrity
- **Runner** (`state/runner.c`): Transaction execution engine running in a separate thread, manages transaction lifecycle and state machine progression
- **State Machine** (`state/sm.c`): Event-driven state machine with circular event queue for processing transaction states
- **API Layer** (`api.c`): Gracht protocol server implementing the served protocol for client communication
- **Transaction States** (`states/*.c`): Individual state handlers for each step of package operations

### Transaction System

The daemon uses an event-driven state machine architecture:
- Each transaction type has a defined state set (sequence of states)
- States execute actions and post events to drive progression
- Runner thread processes transactions at 100ms intervals
- Separate queues for active and waiting transactions
- Wait conditions support: NONE, TRANSACTION (dependency), REBOOT

### Data Flow

1. API receives request → creates transaction in database
2. Runner picks up transaction → initializes state machine
3. State machine executes current state → posts event
4. Event triggers transition to next state
5. Cycle continues until COMPLETED, ERROR, or CANCELLED
6. Transaction cleanup removes from queues and database

## Features

- **Package Management**: Supports common management operations such as install, remove and updates.
- **Robust State Management**: Uses persistent state management, with transactional support to ensure that the system state is robust.
- **Container Support**: Uses containerv (CVD) for application isolation during runtime.


### Dependencies

Served uses minimal dependencies to implement its operations.

- **libfuse3**: On linux, uses FUSE to mount VaFS images.
- **SQLite3**: The state backend is implemented using SQLite

## Windows Implementation

Windows support is planned but not yet implemented. Will require:
- Windows container technology integration
- Windows-specific filesystem mounting
- Windows service management
- Registry integration for package tracking

<h1 align="center" style="margin-top: 0px;">Serve Protocol Specification</h1>

See `protocols/served.gr` file for the complete protocol definition.

```
service served (42) {
    func install(served_install_options options) : () = 1;
    func update(served_update_options options) : () = 2;
    func switch(served_switch_options options) : () = 3;
    func remove(string packageName) : (int result) = 4;
    func info(string packageName) : (served_package info) = 5;
    func listcount() : (uint count) = 6;
    func list() : (served_package[] packages) = 7;
    
    event package_installed : (install_status status, served_package info) = 8;
    event package_removed : (served_package info) = 9;
    event package_updated : (update_status status, served_package info) = 10;
}
```

### API Implementation Status

| Endpoint | Status | Notes |
|----------|--------|-------|
| `install` | ✅ Implemented | Creates INSTALL transaction with package, channel, revision |
| `update` | ⚠️ Partial | Only handles first package in array, needs multi-package support |
| `switch` | ❌ Not Implemented | Channel switching not implemented |
| `remove` | ✅ Implemented | Creates UNINSTALL transaction |
| `info` | ✅ Implemented | Returns package name and version |
| `listcount` | ✅ Implemented | Returns count of installed packages |
| `list` | ✅ Implemented | Returns all installed packages |
| `package_installed` | ❌ Not Implemented | Event not emitted |
| `package_removed` | ❌ Not Implemented | Event not emitted |
| `package_updated` | ❌ Not Implemented | Event not emitted |

## Database Schema

### Tables

- **applications**: Package metadata (name, container_id, flags)
- **commands**: Executable/daemon commands (name, type, path, arguments, pid)
- **revisions**: Package versions (channel, version major/minor/patch/revision)
- **transactions**: Transaction records (type, state, flags, name, description, wait_type, wait_data)
- **transactions_state**: Transaction-specific state (name, channel, revision)

## Known Issues & TODOs

See [TODO.md](TODO.md) for a complete list of pending improvements and missing features.

## Building

The served daemon is built as part of the main Chef project using CMake:

```bash
cd build
cmake ..
make served
```

Binary location: `build/bin/served`

## Running

```bash
# Start the daemon (requires root for container operations and FUSE mounting)
sudo served

# Daemon will:
# 1. Initialize state database
# 2. Start runner thread
# 3. Restore any incomplete transactions
# 4. Start Gracht API server
# 5. Process transactions continuously
```

## Configuration

Configuration files and paths:
- State database: `/var/chef/served/state.db`
- Package storage: `/var/chef/packs/<publisher>/<package>/`
- Mount points: `/chef/apps/<publisher>/<package>/`
- Wrapper scripts: `/chef/bin/<command>`
- Profile script: `/etc/profile.d/chef.sh`
