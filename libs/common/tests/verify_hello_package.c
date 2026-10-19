/** Verify the LCOW smoke-test artifact without extracting it onto NTFS. */
#include <chef/package_manifest.h>
#include <chef/vafs.h>
#include <vafs/reader.h>
#include <vafs/reader/object.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "artifact check failed at line %d: %s\n", __LINE__, #x); goto cleanup; } } while (0)

int main(int argc, char** argv)
{
    struct chef_package_manifest* manifest = NULL;
    struct chef_vafs_codec_context* codec = NULL;
    struct VaFs* image = NULL;
    struct VaFsObjectReader* object = NULL;
    struct VaFsMetadata metadata;
    char bytes[64] = {0};
    int status = 1;
    CHECK(argc == 2);
    CHECK(chef_package_manifest_load(argv[1], &manifest) == 0);
    CHECK(strcmp(manifest->name, "hello-world") == 0);
    CHECK(strcmp(manifest->platform, "linux") == 0);
    CHECK(manifest->commands_count == 1 && strcmp(manifest->commands[0].path, "/usr/bin/hello-world") == 0);
    CHECK(chef_vafs_codec_context_create(&codec) == 0);
    CHECK(chef_vafs_reader_open_file(codec, argv[1], &image) == 0);
    CHECK(vafs_object_reader_open(image, "/usr/bin/hello-world", 0, &object) == 0);
    CHECK(vafs_object_reader_stat(object, &metadata) == 0);
    CHECK(metadata.Type == VaFsEntryType_File && (metadata.Mode & 0111) == 0111);
    CHECK(vafs_object_reader_read(object, bytes, 4) == 4 && memcmp(bytes, "\177ELF", 4) == 0);
    vafs_object_reader_close(object); object = NULL;
    CHECK(vafs_object_reader_open(image, "/usr/bin/hello-link", VaFsLookup_NoFollow, &object) == 0);
    CHECK(vafs_object_reader_stat(object, &metadata) == 0 && metadata.Type == VaFsEntryType_Symlink);
    memset(bytes, 0, sizeof(bytes));
    CHECK(vafs_object_reader_read(object, bytes, sizeof(bytes) - 1) == strlen("hello-world"));
    CHECK(strcmp(bytes, "hello-world") == 0);
    puts("PASS: package manifest, ELF payload, executable modes and relative symlink");
    status = 0;
cleanup:
    if (object) vafs_object_reader_close(object);
    if (image) vafs_reader_close(image);
    chef_vafs_codec_context_destroy(codec);
    chef_package_manifest_free(manifest);
    return status;
}
