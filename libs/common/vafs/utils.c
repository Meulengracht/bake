/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <chef/list.h>
#include <chef/package.h>
#include <chef/package_manifest.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void __free_version(struct chef_version* version)
{
    free((void*)version->created);
    free((void*)version->tag);
}

static void __free_revision(struct chef_revision* revision)
{
    free((void*)revision->channel);
    free((void*)revision->platform);
    free((void*)revision->architecture);
    __free_version(&revision->current_version);
}

static void __free_string_item(void* item)
{
    struct list_item_string* stringItem = item;

    free((void*)stringItem->value);
    free(stringItem);
}

void chef_package_proof_free(struct chef_package_proof* proof)
{
    if (proof == NULL) {
        return;
    }

    free((void*)proof->identity);
    free((void*)proof->hash_algorithm);
    free((void*)proof->hash);
    free((void*)proof->public_key);
    free((void*)proof->signature);
    free(proof);
}

void chef_package_free(struct chef_package* package)
{
    if (package == NULL) {
        return;
    }

    free((void*)package->platform);
    free((void*)package->arch);
    free((void*)package->publisher);
    free((void*)package->package);
    free((void*)package->base);
    free((void*)package->summary);
    free((void*)package->description);
    free((void*)package->homepage);
    free((void*)package->license);
    free((void*)package->eula);
    free((void*)package->maintainer);
    free((void*)package->maintainer_email);

    if (package->revisions != NULL) {
        for (size_t i = 0; i < package->revisions_count; i++) {
            __free_revision(&package->revisions[i]);
        }
        free(package->revisions);
    }
    free(package);
}

void chef_version_free(struct chef_version* version)
{
    if (version == NULL) {
        return;
    }

    __free_version(version);
    free(version);
}

void chef_commands_free(struct chef_command* commands, int count)
{
    if (commands == NULL || count == 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((void*)commands[i].name);
        free((void*)commands[i].path);
        free((void*)commands[i].arguments);
        free((void*)commands[i].description);
        free((void*)commands[i].icon_buffer);
    }
    free(commands);
}

void chef_package_application_config_free(struct chef_package_application_config* appConfig)
{
    if (appConfig == NULL) {
        return;
    }

    free((void*)appConfig->network_gateway);
    free((void*)appConfig->network_dns);
    free(appConfig);
}

void chef_package_capabilities_free(struct chef_package_capability* capabilities, int count)
{
    if (capabilities == NULL || count == 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((void*)capabilities[i].name);
        list_destroy(&capabilities[i].config.network_client.allow, __free_string_item);
    }
    free(capabilities);
}

int chef_version_from_string(const char* string, struct chef_version* version)
{
    return chef_package_manifest_parse_version(string, version);
}
