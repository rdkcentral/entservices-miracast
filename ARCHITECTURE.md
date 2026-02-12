# Miracast System Architecture

## Overall System Architecture

The Miracast system is designed as a modular, plugin-based wireless display streaming solution built on the WPEFramework (Thunder). The architecture consists of two primary plugins:

- **MiracastService**: Orchestrates device discovery, connection management, and session control
- **MiracastPlayer**: Handles media streaming, RTSP negotiation, and playback using GStreamer

The system leverages industry-standard protocols including Wi-Fi Direct (P2P), RTSP, and RTP for secure device pairing and high-quality media streaming.

## Component Interactions & Data Flow

### Core Components
1. **MiracastService Plugin**
   - Manages P2P device discovery and connection establishment
   - Handles Thunder/JSON-RPC API requests
   - Controls session lifecycle and state management
   - Interfaces with system services via IARMBus

2. **MiracastPlayer Plugin** 
   - Processes RTSP messages for session negotiation
   - Manages GStreamer pipelines for media playback
   - Handles stream control (play, pause, stop, teardown)
   - Reports playback state and events

3. **P2P Module (MiracastP2P)**
   - Wi-Fi Direct device discovery and pairing
   - WPA supplicant interface management
   - Network configuration and IP assignment

4. **RTSP Module (MiracastRTSPMsg)**
   - RTSP protocol implementation (M1-M7 message exchange)
   - Stream parameter negotiation
   - Keep-alive and teardown handling

### Data Flow Sequence
```
Client Request → MiracastService → P2P Discovery → Device Pairing
                     ↓
P2P Connection Established → RTSP Session Negotiation → MiracastPlayer
                     ↓
GStreamer Pipeline Creation → Media Stream Processing → Display Output
```

## Plugin Framework Integration

### WPEFramework Plugin Architecture
- Plugins implement `PluginHost::IPlugin` interface for lifecycle management
- JSON-RPC communication via `PluginHost::JSONRPC` inheritance
- Event notification system using `INotification` interfaces
- Configuration management through `.conf.in` files

### Plugin Lifecycle
1. **Initialization**: Plugin registration and dependency resolution
2. **Configuration**: Parameter loading from config files
3. **Activation**: Service startup and resource allocation
4. **Operation**: Request handling and event processing
5. **Deactivation**: Cleanup and resource release

### Inter-Plugin Communication
- Direct interface calls for synchronous operations
- Event notifications for asynchronous state changes
- Shared common utilities for logging and synchronization

## Dependencies & Interfaces

### External Dependencies
- **WPEFramework**: Core plugin framework and JSON-RPC infrastructure
- **GStreamer**: Multimedia framework for stream processing
- **GLib**: Core utilities and event loop management
- **WPA Supplicant**: Wi-Fi Direct P2P operations
- **IARMBus**: Inter-process communication with system services

### System Interfaces
- **Thunder/JSON-RPC**: External API for client applications
- **IARMBus**: System service integration (power management, device control)
- **WiFi/P2P**: Network layer for device connectivity
- **Display/Graphics**: Output rendering subsystem

### Internal Interfaces
- `IMiracastService`: Service control and event notifications
- `IMiracastPlayer`: Player control and state management
- Common utilities: Logging, synchronization, message passing

## Technical Implementation Details

### Thread Architecture
- **Main Thread**: Plugin lifecycle and JSON-RPC handling
- **P2P Monitor Thread**: Wi-Fi Direct event processing
- **RTSP Handler Thread**: Protocol message processing
- **GStreamer Threads**: Media pipeline processing (managed internally)

### State Management
- Controller framework states for connection lifecycle
- Player states for stream processing status
- Synchronized state transitions with proper error handling

### Error Handling & Recovery
- Comprehensive error codes and reason descriptions
- Graceful degradation and connection retry mechanisms
- Logging infrastructure for debugging and monitoring

### Security Considerations
- WPA2/WPA3 encryption for P2P connections
- Device authentication through Wi-Fi Direct protocols
- Secure RTSP session establishment

### Performance Optimizations
- Efficient message queuing for inter-thread communication
- Optimized GStreamer pipeline configurations
- Resource pooling and connection reuse
- Adaptive streaming based on network conditions