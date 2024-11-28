#include "aggregator.h"

int normalize_update(model_upd_t *update, uint64_t latest_version)
{

    int fd = open(update->file_name, O_RDONLY);
    if (fd == -1)
    {
        perror("Failed to open model update file");
        return -1;
    }

    model_file_info_t model_info = {0};
    if (load_model_info_from_file(fd, &model_info) < 0)
    {
        perror("Failed to load model info from file");
        return -1;
    }

    close(fd);

    if (!mfi_is_diff_format(model_info))
    {
        perror("Model is not in diff format");
        return -1;
    }

    if (model_info.diffed_from_model_version != latest_version)
    {
        perror("Strugglers not yet supported");
        return -1;
    }

    return 0;
}

void model_queue_thread(void *_args)
{
    set_debug(1);
    model_upd_t *updates[MAX_PENDING_MODEL_UPDATES];
    size_t updates_index = 0;

    model_upd_t *update;
    struct timespec last_aggregation = {0};
    agg_config_t agg_config = {0};

    int stat = 0;
    while (1)
    {
        if (agg_config.type == 2)
            stat = queue_model_upd_tdequeue(&model_queue, &update, &agg_config.ts);
        else
            stat = queue_model_upd_dequeue(&model_queue, &update);

        if (stat < 0)
        {
            if (stat == QUEUE_CLOSED)
            {
                return;
            }

            perror("Failed to dequeue model update");
            return;
        }

        if (normalize_update(update, global_model_id) >= 0)
        {
            updates[updates_index++] = update;
        }

        should_aggregate_models(updates_index, &last_aggregation, &agg_config);
        if (agg_config.type == 0)
        {
            continue;
        }

        if (agg_config.type == 1)
        {
            printf("Aggregating %zu models\n", updates_index);

            int new_id = aggregate_models(updates, updates_index);
            if (new_id < 0)
            {
                debug_print("Failed to aggregate models\n");
                perror("Failed to aggregate models");
                continue;
            }

            debug_print("Aggregated models\n");

            set_global_model_id(new_id);

            for (size_t i = 0; i < updates_index; i++)
            {
                if (remove(updates[i]->file_name) == -1)
                {
                    perror("Failed to remove model update file");
                    return;
                }
                free(updates[i]);
            }
            updates_index = 0;
        }
    }
}