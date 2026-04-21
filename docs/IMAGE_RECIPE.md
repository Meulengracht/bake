
<h1 align="center" style="margin-top: 0px;">Image Recipe Specification</h1>


```
#########################
# schema - Required
#    values: {mbr, gpt}
#
# The disk partitioning schema to use. This determines the partition
# table format for the disk image.
#  - mbr: Master Boot Record (MBR) partitioning
#  - gpt: GUID Partition Table (GPT) partitioning
schema: gpt

#########################
# partitions - Required
#
# A list of partitions to create in the disk image. Each partition
# defines a filesystem and its contents.
partitions:
    ###########################
    # label - Required
    #
    # The partition label. Must only contain [a-zA-Z_-] characters.
    # This will be used to identify the partition in the system.
  - label: my-partition

    ###########################
    # type - Required
    #
    # The filesystem type to format the partition with. Common values
    # include fat12, fat16, fat32, mfs, or other filesystem types
    # supported by mkcdk.
    type: fat32

    ###########################
    # guid - Required for GPT
    #
    # The partition type GUID. This is required when schema is 'gpt'.
    # Should be a standard GUID format (XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX).
    # Common partition type GUIDs:
    #  - EFI System: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    #  - Microsoft Basic Data: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
    #  - Linux Filesystem: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
    guid: C12A7328-F81F-11D2-BA4B-00A0C93EC93B

    ###########################
    # id - Optional
    #
    # The partition ID. This can specify a partition type byte (for MBR)
    # and/or a GUID. Supported formats:
    #  - "XX": Partition type byte only (hex, e.g., "0C" for FAT32 LBA)
    #  - "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX": GUID only
    #  - "XX, XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX": Both type and GUID
    #
    # Note: When schema is 'gpt', the 'guid' field is required. The 'id'
    # field can provide the GUID in the "GUID" or "XX, GUID" formats,
    # which will satisfy the guid requirement. If both 'id' and 'guid'
    # are provided, 'guid' takes precedence.
    id: 0C

    ###########################
    # size - Optional
    #
    # The partition size in bytes. If not specified for the last partition,
    # it will use remaining space. The parser uses strtoll() to parse this
    # value, so it expects a numeric value in bytes.
    #
    # Note: While examples may use suffixes like "128MB", these are NOT
    # parsed by the image parser. They may be interpreted by other tools
    # in the build chain, but the parser itself only reads the numeric value.
    # To be safe, always specify sizes in bytes (e.g., 134217728 for 128MB).
    size: 134217728

    ###########################
    # content - Optional
    #
    # Path to content to populate the partition with. This can be used
    # to specify a pre-built filesystem image or raw data file.
    content: /path/to/content.img

    ###########################
    # attributes - Optional
    #
    # A list of partition attributes/flags to set. Common attributes include:
    #  - boot: Mark as bootable partition
    #  - readonly: Mark partition as read-only
    #  - noautomount: Prevent automatic mounting
    # The specific attributes supported depend on the partition schema and
    # filesystem type.
    attributes:
      - boot
      - readonly

    ###########################
    # fat-options - Optional
    #
    # FAT filesystem specific options. Only applicable when type is a FAT
    # variant (fat12, fat16, fat32).
    fat-options:
      ###########################
      # reserved-image - Optional
      #
      # Path to a pre-built image file to use for the reserved sectors
      # of the FAT filesystem. This is typically used for boot loaders.
      reserved-image: /path/to/bootloader.img

    ###########################
    # sources - Optional
    #
    # A list of source mappings that define how to populate the partition's
    # filesystem. Each source entry specifies content to copy into the partition.
    sources:
        ###########################
        # type - Required
        #    values: {file, dir, package, raw}
        #
        # The type of source being added:
        #  - file: Copy a single file
        #  - dir: Recursively copy a directory
        #  - package: Extract and copy files from a Chef package
        #  - raw: Write raw data directly to a specific sector
        #
        # Note: Examples may use 'type: chef' but the parser only recognizes
        # 'package' as the valid value for Chef packages.
      - type: file

        ###########################
        # source - Required
        #
        # The source path, which interpretation depends on the type:
        #  - file/dir: Path to the file or directory on the host system
        #  - package: Package identifier (e.g., "publisher/package/channel")
        #  - raw: Path to the raw data file
        source: /path/to/source/file.txt

        ###########################
        # target - Required
        #
        # The target path, which interpretation depends on the type:
        #  - file/dir: Destination path within the partition's filesystem
        #  - package: Destination directory within the partition
        #  - raw: Target location specification (e.g., "sector=0")
        target: /destination/path/file.txt

      - type: dir
        source: /path/to/source/directory
        target: /destination/directory

      - type: package
        source: publisher/package/channel
        target: /install/path

      - type: raw
        source: /path/to/raw/data.img
        target: sector=0
```

## Parser Implementation Notes

The authoritative YAML syntax is defined by the parser implementation in `libs/common/image.c`. Key behaviors to be aware of:

- **Schema requirement**: The `schema` field must be set to either `mbr` or `gpt`.
- **GPT GUID requirement**: When `schema` is `gpt`, each partition must have a `guid` field. The `id` field can also provide the GUID using the "GUID" or "XX, GUID" formats.
- **Size parsing**: The `size` field is parsed using `strtoll()`, which only reads the numeric portion. Unit suffixes like "MB" or "GB" in YAML examples are not interpreted by the parser itself.
- **Source type values**: The parser recognizes `file`, `dir`, `package`, and `raw` as valid source types. Examples using `type: chef` should be updated to use `type: package`.

## Examples

Example image recipe files can be found in the repository:

- [`examples/images/simple-fat.yaml`](examples/images/simple-fat.yaml) - MBR disk with FAT32 partition, demonstrating file, directory, and raw sources
- [`examples/images/fat.yaml`](examples/images/fat.yaml) - Simple MBR partition with boot attribute
- [`examples/images/uefi-gpt.yaml`](examples/images/uefi-gpt.yaml) - GPT disk with multiple partitions for UEFI boot, demonstrating various partition configurations
