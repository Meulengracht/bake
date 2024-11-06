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

#include <chef/client.h>
#include <chef/dirs.h>
#include <chef/kitchen.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <chef/remote.h>
#include <chef/storage/download.h>
#include <errno.h>
#include <libfridge.h>
#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <threading.h>
#include <vlog.h>

struct __cookd_queue {
    // remember volatility means nothing in terms of memory
    // safety, but rather avoid the compiler optimizing the 
    // checks.
    volatile int active;
    mtx_t        lock;
    cnd_t        signal;
    struct list  queue;
};

struct __cookd_builder_request {
    struct list_item           list_header;
    char*                      id;
    struct cookd_build_options options;
};

static struct __cookd_builder_request* __cookd_builder_request_new(const char* id, struct cookd_build_options* options)
{
    struct __cookd_builder_request* request;

    request = calloc(1, sizeof(struct __cookd_builder_request));
    if (request == NULL) {
        return NULL;
    }

    request->id = platform_strdup(id);
    request->options.architecture = platform_strdup(options->architecture);
    request->options.platform = platform_strdup(options->platform);
    request->options.url = platform_strdup(options->url);
    request->options.recipe_path = platform_strdup(options->recipe_path);
    return request;
}

static void __cookd_builder_request_delete(struct __cookd_builder_request* request)
{
    if (request == NULL) {
        return;
    }
    free((char*)request->options.architecture);
    free((char*)request->options.platform);
    free((char*)request->options.recipe_path);
    free((char*)request->options.url);
    free(request->id);
    free(request);
}

enum __cookd_builder_state {
    __COOKD_BUILDER_STATE_CREATED,
    __COOKD_BUILDER_STATE_RUNNING,
    __COOKD_BUILDER_STATE_DONE
};

struct __cookd_builder {
    struct list_item           list_header;
    thrd_t                     builder_id;
    enum __cookd_builder_state state;
    struct __cookd_queue*      queue;
};

static struct __cookd_builder* __cookd_builder_new(struct __cookd_queue* queue)
{
    struct __cookd_builder* builder;

    builder = calloc(1, sizeof(struct __cookd_builder));
    if (builder == NULL) {
        return NULL;
    }
    builder->state = __COOKD_BUILDER_STATE_CREATED;
    builder->queue = queue;
    return builder;
}

static void __cookd_builder_delete(struct __cookd_builder* builder)
{
    if (builder == NULL) {
        return;
    }
    free(builder);
}

static int __cookd_server_build(const char* id, struct cookd_build_options* options);

static int __cookd_builder_main(void* arg)
{
    struct __cookd_builder*         this = arg;
    struct __cookd_builder_request* request;
    VLOG_DEBUG("server", "__cookd_builder_main()\n");

    // update state
    this->state = __COOKD_BUILDER_STATE_RUNNING;

    for (;;) {
        mtx_lock(&this->queue->lock);

        // cancellation point, so we can cleanup properly
        if (!this->queue->active) {
            mtx_unlock(&this->queue->lock);
            break;
        }

        cnd_wait(&this->queue->signal, &this->queue->lock);

        // cancellation point, so we can cleanup properly
        if (!this->queue->active) {
            mtx_unlock(&this->queue->lock);
            break;
        }

        // pop request
        request = (struct __cookd_builder_request*)this->queue->queue.head;
        if (request != NULL) {
            this->queue->queue.head = request->list_header.next;
        }
        mtx_unlock(&this->queue->lock);
        __cookd_server_build(request->id, &request->options);
        __cookd_builder_request_delete(request);
    }

    // update state again
    this->state = __COOKD_BUILDER_STATE_DONE;
    return 0;
}

static int __cookd_builder_start(struct __cookd_builder* builder)
{
    int status;
    VLOG_DEBUG("server", "__cookd_builder_start()\n");

    status = thrd_create(&builder->builder_id, __cookd_builder_main, builder);
    if (status != thrd_success) {
        return -1;
    }
    return 0;
}

struct __cookd_server {
    struct __cookd_queue queue;
    struct list          builders;
};

static struct __cookd_server* __cookd_server_new(void)
{
    struct __cookd_server* server;

    server = calloc(1, sizeof(struct __cookd_server));
    if (server == NULL) {
        return NULL;
    }

    mtx_init(&server->queue.lock, mtx_plain);
    cnd_init(&server->queue.signal);
    server->queue.active = 1;
    return server;
}

static void __cookd_server_delete(struct __cookd_server* server)
{
    struct list_item* li;

    if (server == NULL) {
        return;
    }

    list_destroy(&server->builders, (void(*)(void*))__cookd_builder_delete);
    list_destroy(&server->queue.queue, (void(*)(void*))__cookd_builder_request_delete);
    mtx_destroy(&server->queue.lock);
    cnd_destroy(&server->queue.signal);
}

static int __cookd_server_start(struct __cookd_server* server, int builderCount)
{
    VLOG_DEBUG("server", "__cookd_server_start(builders=%i)\n", builderCount);

    for (int i = 0; i < builderCount; i++) {
        struct __cookd_builder* builder = __cookd_builder_new(&server->queue);
        if (builder == NULL) {
            VLOG_ERROR("server", "failed to allocate memory for builder\n");
            return -1;
        }
        list_add(&server->builders, &builder->list_header);

        if (__cookd_builder_start(builder)) {
            VLOG_ERROR("server", "failed to start builder %i\n", i);
            return -1;
        }
    }
    return 0;
}

static void __cookd_server_stop(struct __cookd_server* server)
{
    VLOG_DEBUG("server", "__cookd_server_stop()\n");

    // grab the lock first, so we can serialize the access
    // to the active member
    mtx_lock(&server->queue.lock);

    // mark queue inactive
    server->queue.active = 0;

    // wait for builders to stop, active builds can
    // taken a while, so we may have to sleep here for 
    // now
    // FIXME: timeout
    VLOG_DEBUG("server", "__cookd_server_stop: stopping builders\n");
    for (;;) {
        struct list_item* li;
        int               waiting = 0;

        cnd_broadcast(&server->queue.signal);
        mtx_unlock(&server->queue.lock);

        list_foreach(&server->builders, li) {
            struct __cookd_builder* builder = (struct __cookd_builder*)li;
            if (builder->state != __COOKD_BUILDER_STATE_DONE) {
                VLOG_DEBUG("server", "waiting for builder %i to shut down...\n", (int)builder->builder_id);
                waiting = 1;
                break;
            }
        }

        if (!waiting) {
            // means we don't need to unlock out of loop
            break;
        }

        // do an update every 10s
        platform_sleep(10 * 1000);
        mtx_lock(&server->queue.lock);
    }
}

static struct __cookd_server* g_server = NULL;

int cookd_server_init(int builderCount)
{
    int status;
    VLOG_DEBUG("server", "cookd_server_init(builders=%i)\n", builderCount);

    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("cookd", "failed to initialize chef client\n");
        return -1;
    }

    status = fridge_initialize(CHEF_PLATFORM_STR, CHEF_ARCHITECTURE_STR);
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize fridge\n");
        chefclient_cleanup();
        return status;
    }

    g_server = __cookd_server_new();
    if (g_server == NULL) {
        VLOG_ERROR("cookd", "failed to allocate memory for server\n");
        fridge_cleanup();
        chefclient_cleanup();
        return -1;
    }

    status = __cookd_server_start(g_server, builderCount);
    if (status) {
        VLOG_ERROR("cookd", "failed to start cookd server\n");
        __cookd_server_delete(g_server);
        fridge_cleanup();
        chefclient_cleanup();
        return status;
    }
    return 0;
}

void cookd_server_cleanup(void)
{
    VLOG_DEBUG("server", "cookd_server_cleanup()\n");

    __cookd_server_stop(g_server);
    __cookd_server_delete(g_server);
    fridge_cleanup();
    chefclient_cleanup();
}

void cookd_server_status(struct cookd_status* status)
{
    VLOG_DEBUG("server", "cookd_server_status()\n");

    if (g_server == NULL) {
        status->queue_size = 0;
        return;
    }

    status->queue_size = g_server->queue.queue.count;
}

static int __add_kitchen_ingredient(const char* name, const char* path, struct list* kitchenIngredients)
{
    struct kitchen_ingredient* ingredient;
    VLOG_DEBUG("cookd", "__add_kitchen_ingredient(name=%s, path=%s)\n", name, path);

    ingredient = malloc(sizeof(struct kitchen_ingredient));
    if (ingredient == NULL) {
        return -1;
    }
    memset(ingredient, 0, sizeof(struct kitchen_ingredient));

    ingredient->name = name;
    ingredient->path = path;

    list_add(kitchenIngredients, &ingredient->list_header);
    return 0;
}

static int __prep_toolchains(struct list* platforms, struct list* kitchenIngredients)
{
    struct list_item* item;
    VLOG_DEBUG("cookd", "__prep_toolchains()\n");

    list_foreach(platforms, item) {
        struct recipe_platform* platform = (struct recipe_platform*)item;
        int                     status;
        const char*             path;
        char*                   name;
        char*                   channel;
        char*                   version;
        if (platform->toolchain == NULL) {
            continue;
        }
        
        status = recipe_parse_platform_toolchain(platform->toolchain, &name, &channel, &version);
        if (status) {
            VLOG_ERROR("cookd", "failed to parse toolchain %s for platform %s", platform->toolchain, platform->name);
            return status;
        }

        status = fridge_ensure_ingredient(&(struct fridge_ingredient) {
            .name = name,
            .channel = channel,
            .version = version,
            .arch = CHEF_ARCHITECTURE_STR,
            .platform = CHEF_PLATFORM_STR
        }, &path);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("cookd", "failed to fetch ingredient %s\n", name);
            return status;
        }
        
        status = __add_kitchen_ingredient(name, path, kitchenIngredients);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("cookd", "failed to mark ingredient %s\n", name);
            return status;
        }
    }
    return 0;
}

static int __prep_ingredient_list(struct list* list, const char* platform, const char* arch, struct list* kitchenIngredients)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("cookd", "__prep_ingredient_list(platform=%s, arch=%s)\n", platform, arch);

    list_foreach(list, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        const char*               path = NULL;

        status = fridge_ensure_ingredient(&(struct fridge_ingredient) {
            .name = ingredient->name,
            .channel = ingredient->channel,
            .version = ingredient->version,
            .arch = arch,
            .platform = platform
        }, &path);
        if (status) {
            VLOG_ERROR("cookd", "failed to fetch ingredient %s\n", ingredient->name);
            return status;
        }
        
        status = __add_kitchen_ingredient(ingredient->name, path, kitchenIngredients);
        if (status) {
            VLOG_ERROR("cookd", "failed to mark ingredient %s\n", ingredient->name);
            return status;
        }
    }
    return 0;
}

static int __prep_ingredients(struct recipe* recipe, const char* platform, const char* arch, struct kitchen_setup_options* kitchenOptions)
{
    struct list_item* item;
    int               status;

    if (recipe->platforms.count > 0) {
        VLOG_TRACE("cookd", "preparing %i platforms\n", recipe->platforms.count);
        status = __prep_toolchains(
            &recipe->platforms,
            &kitchenOptions->host_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.host.ingredients.count > 0) {
        VLOG_TRACE("cookd", "preparing %i host ingredients\n", recipe->environment.host.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.host.ingredients,
            CHEF_PLATFORM_STR,
            CHEF_ARCHITECTURE_STR,
            &kitchenOptions->host_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.build.ingredients.count > 0) {
        VLOG_TRACE("cookd", "preparing %i build ingredients\n", recipe->environment.build.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.build.ingredients,
            platform,
            arch,
            &kitchenOptions->build_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.runtime.ingredients.count > 0) {
        VLOG_TRACE("cookd", "preparing %i runtime ingredients\n", recipe->environment.runtime.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.runtime.ingredients,
            platform,
            arch,
            &kitchenOptions->runtime_ingredients
        );
        if (status) {
            return status;
        }
    }
    return 0;
}

// <root> / <id> / sources / 
// <root> / <id> / src.image
static int __prepare_sources(const char* id, const char* url, char** projectPathOut)
{
    char* buildRoot;
    char* imagePath = NULL;
    char* projectPath = NULL;
    int   status;
    VLOG_DEBUG("server", "__prepare_sources(id=%s, url=%s)\n", id, url);

    buildRoot = strpathcombine(chef_dirs_root(), id);
    if (buildRoot == NULL) {
        VLOG_ERROR("cookd", "__prepare_sources: failed to allocate memory for build path\n");
        return -1;
    }

    imagePath = strpathcombine(buildRoot, "src.image");
    if (imagePath == NULL) {
        VLOG_ERROR("cookd", "__prepare_sources: failed to allocate memory for source image\n");
        goto cleanup;
    }

    projectPath = strpathcombine(buildRoot, "sources");
    if (projectPath == NULL) {
        VLOG_ERROR("cookd", "__prepare_sources: failed to allocate memory for source root\n");
        goto cleanup;
    }

    // create folder for build id
    status = platform_mkdir(&buildRoot[0]);
    if (status) {
        VLOG_ERROR("cookd", "__prepare_sources: failed to create build project directory %s for build id %s\n", &buildRoot[0], id);
        goto cleanup;
    }

    // download source first to the image path
    status = chef_client_gen_download(url, imagePath);
    if (status) {
        VLOG_ERROR("cookd", "__prepare_sources: failed to download %s for build id %s\n", url, id);
        goto cleanup;
    }

    // unpack it using our unmkvafs tool
    status = remote_unpack(imagePath, projectPath);
    if (status) {
        VLOG_ERROR("cookd", "__prepare_sources: failed to unpack %s for build id %s\n", imagePath, id);
        goto cleanup;
    }

    *projectPathOut = projectPath;

cleanup:
    if (status) {
        free(projectPath);
    }
    free(imagePath);
    free(buildRoot);
    return status;
}

static int __load_recipe(const char* projectPath, const char* recipePath, struct recipe** recipeOut)
{
    struct recipe* recipe;
    char*          combined = NULL;
    void*          buffer = NULL;
    size_t         length;
    int            status;
    VLOG_DEBUG("server", "__load_recipe(proj=%s, path=%s)\n", projectPath, recipePath);
    
    // build the absolute path for the recipe
    combined = strpathcombine(projectPath, recipePath);
    if (combined == NULL) {
        VLOG_ERROR("cookd", "__load_recipe: ran out of memory for path allocations\n");
        return -1;
    }

    status = platform_readfile(combined, &buffer, &length);
    if (status) {
        VLOG_ERROR("cookd", "__load_recipe: failed to read recipe %s\n", combined);
        goto cleanup;
    }

    status = recipe_parse(buffer, length, &recipe);
    if (status) {
        VLOG_ERROR("cookd", "__load_recipe: failed to parse recipe %s\n", combined);
    }

    *recipeOut = recipe;

cleanup:
    free(combined);
    free(buffer);
    return status;
}

static int __cookd_server_build(const char* id, struct cookd_build_options* options)
{
    struct kitchen_setup_options setupOptions = { 0 };
    struct kitchen               kitchen;
    int                          status;
    char*                        projectPath;
    struct recipe*               recipe;
    VLOG_DEBUG("server", "__cookd_server_build(id=%s, url=%s)\n", id, options->url);

    status = __prepare_sources(id, options->url, &projectPath);
    if (status) {
        VLOG_ERROR("cookd", "failed to prepare sources for build id %s (%s)\n", id, options->url);
        return status;
    }

    status = __load_recipe(projectPath, options->recipe_path, &recipe);
    if (status) {
        VLOG_ERROR("cookd", "failed to load the recipe for build id %s (%s)\n", id, options->recipe_path);
        return status;
    }

    status = kitchen_initialize(&(struct kitchen_init_options) {
        .envp = NULL, /* not used currently */
        .recipe = recipe,
        .recipe_path = options->recipe_path,
        .project_path = projectPath,
        .target_architecture = options->architecture,
        .target_platform = options->platform
    }, &kitchen);
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize kitchen area for build id %s\n", id);
        return status;
    }

    status = __prep_ingredients(recipe, options->platform, options->architecture, &setupOptions);
    if (status) {
        VLOG_ERROR("cookd", "failed to fetch ingredients for build id %s: %s\n", id, strerror(errno));
        goto cleanup;
    }

    // setup linux options
    setupOptions.packages = &recipe->environment.host.packages;

    // setup kitchen hooks
    setupOptions.setup_hook.bash = recipe->environment.hooks.bash;
    setupOptions.setup_hook.powershell = recipe->environment.hooks.powershell;
    status = kitchen_setup(&kitchen, &setupOptions);
    if (status) {
        VLOG_ERROR("cookd", "failed to setup kitchen area for build id %s\n", id);
        goto cleanup;
    }

    status = kitchen_recipe_source(&kitchen);
    if (status) {
        VLOG_ERROR("cookd", "failed to resolve sources for build id %s\n", id);
        goto cleanup;
    }
    
    status = kitchen_recipe_make(&kitchen);
    if (status) {
        VLOG_ERROR("cookd", "failed to build project for build id %s\n", id);
        goto cleanup;
    }

    status = kitchen_recipe_pack(&kitchen);
    if (status) {
        VLOG_ERROR("cookd", "failed to pack project artifacts for build id %s\n", id);
    }

cleanup:
    kitchen_destroy(&kitchen);
    fridge_cleanup();
    return status;
}

int cookd_server_queue_build(const char* id, struct cookd_build_options* options)
{
    struct __cookd_builder_request* request;
    VLOG_DEBUG("server", "cookd_server_queue_build(id=%s, url=%s)\n", id, options->url);

    request = __cookd_builder_request_new(id, options);
    if (request == NULL) {
        return -1;
    }

    mtx_lock(&g_server->queue.lock);
    list_add(&g_server->queue.queue, &request->list_header);
    cnd_signal(&g_server->queue.signal);
    mtx_unlock(&g_server->queue.lock);
    return 0;
}
