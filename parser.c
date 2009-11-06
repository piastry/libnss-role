#include <Role/parser.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int graph_add(struct graph *G, struct ver v)
{
	if (G->size == G->capacity) {
		G->capacity <<= 1;
		G->gr = (struct ver *) realloc(G->gr, sizeof(struct ver) * G->capacity);
		if (!G->gr)
			return MEMORY_ERROR;
		G->used = (int *) malloc(sizeof(int) * G->capacity);
		if (!G->used)
			return MEMORY_ERROR;
	}
	G->gr[G->size++] = v;
	return OK;
}

int ver_add(struct ver *v, gid_t g)
{
	if (v->size == v->capacity) {
		v->capacity <<= 1;
		v->list = (gid_t *) realloc(v->list, sizeof(gid_t) * v->capacity);
		if (!v->list)
			return MEMORY_ERROR;
	}
	v->list[v->size++] = g;
	return OK;
}

int graph_init(struct graph *G)
{
	G->gr = (struct ver *) malloc(sizeof(struct ver) * G->capacity);
	if (!G->gr)
		return MEMORY_ERROR;

	G->used = (int *) malloc(sizeof(int) * G->capacity);
	if (!G->used)
		return MEMORY_ERROR;

	return OK;
}

int ver_init(struct ver *v)
{
	v->list = (gid_t *) malloc(sizeof(gid_t) * v->capacity);
	if (!v->list)
		return MEMORY_ERROR;

	return OK;
}

int find_id(struct graph *G, gid_t g, int *id)
{
	int i;
	for(i = 0; i < G->size; i++)
		if (G->gr[i].gid == g) {
			*id = i;
			return OK;
		}

	return NO_SUCH_GROUP;
}

void free_all(struct graph *G)
{
	int i;
	for(i = 0; i < G->size; i++) {
		free(G->gr[i].list);
	}
	free(G->gr);
	free(G->used);
}

int realloc_groups(long int **size, gid_t ***groups, long int new_size)
{
	gid_t *new_groups;

	new_groups = (gid_t *)
		realloc((**groups),
			new_size * sizeof(***groups));

	if (!new_groups)
		return MEMORY_ERROR;

	**groups = new_groups;
	**size = new_size;

	return OK;
}

static int parse_line(char *s, struct graph *G)
{
	int result;
	unsigned long len = strlen(s);
	int i;
	struct group_name role_name = {{0}, 0};
	struct ver role = {0, 0, 0, 10};

	result = ver_init(&role);
	if (result != OK)
		return result;

	for(i = 0; i < len; i++) {
		if (s[i] == ':') {
			i++;
			break;
		}
		role_name.name[role_name.id++] = s[i];
	}

	result = get_gid(role_name.name, &role.gid);
	if (result != OK)
		goto libnss_role_parse_line_error;

	while(1) {
		struct group_name gr_name = {{0}, 0};
		if (i >= len)
			break;
		gid_t gr;
		for(; i < len; i++) {
			if (s[i] == ',') {
				i++;
				break;
			}
			gr_name.name[gr_name.id++] = s[i];
		}

		result = get_gid(gr_name.name, &gr);
		if (result != OK && result != NO_SUCH_GROUP)
			goto libnss_role_parse_line_error;
		else if (result == NO_SUCH_GROUP)
			continue;

		result = ver_add(&role, gr);
		if (result != OK)
			goto libnss_role_parse_line_error;
	}

	result = graph_add(G, role);
	if (result != OK)
		goto libnss_role_parse_line_error;
	return result;

libnss_role_parse_line_error:
	free(role.list);
	return result;
}

int reading(const char *s, struct graph *G)
{
	int result = OK;
	FILE *f = NULL;
	unsigned long len = STR_min_size;
	char *str = NULL;
	unsigned long id = 0;
	char c;

	f = fopen(s, "r");
	if (!f) {
		result = IO_ERROR;
		goto libnss_role_reading_out;
	}

	str = malloc(len * sizeof(char));
	if (!str)
		goto libnss_role_reading_close;

	while(1) {
		c = fgetc(f);
		if (c == EOF)
			break;
		if (c == '\n') {
			str[id] = '\0';
			result = parse_line(str, G);
			if (result != OK)
				goto libnss_role_reading_close;
			id = 0;
			continue;
		}
		str[id++] = c;
		if (id == len) {
			len <<= 1;
			str = realloc(str, len * sizeof(char));
			if (!str) {
				result = MEMORY_ERROR;
				goto libnss_role_reading_close;
			}
		}
	}
	if (id) {
		str[id] = '\0';
		result = parse_line(str, G);
		if (result != OK)
			goto libnss_role_reading_close;
	}
	
libnss_role_reading_close:
	fclose(f);
libnss_role_reading_out:
	free(str);
	return result;
}

int dfs(struct graph *G, gid_t v, group_collector *col)
{
	int i, j, result;

	result = find_id(G, v, &i);
	if (result != OK) {
		result = ver_add(col, v);
		return result;
	}

	if (G->used[i])
		return OK;
	result = ver_add(col, v);
	if (result != OK)
		return result;
	G->used[i] = 1;

	for(j = 0; j < G->gr[i].size; j++) {
		result = dfs(G, G->gr[i].list[j], col);
		if (result != OK && result != NO_SUCH_GROUP)
			return result;
	}
	return OK;
}

int get_gid(char *gr_name, gid_t *ans)
{
	if (!isdigit(gr_name[0])) {
		struct group grp, *grp_ptr;
		char buffer[1000];
		if (getgrnam_r(gr_name, &grp, buffer, 1000, &grp_ptr) == 0) {
			if (errno == ERANGE)
				return OUT_OF_RANGE;
			if (errno != 0)
				return UNKNOWN_ERROR;
		}
		if (!grp_ptr)
			return NO_SUCH_GROUP;
		*ans = grp.gr_gid;
		return OK;
	}

	if (sscanf(gr_name, "%u", ans) < 1)
		return IO_ERROR;

	return OK;
}

int writing(const char *file, struct graph *G)
{
	int i, j, result = IO_ERROR;
	FILE *f = fopen(file, "w");
	if (!f)
		return result;

	for(i = 0; i < G->size; i++) {
		if (!G->gr[i].size)
			continue;
		if (fprintf(f, "%u:", G->gr[i].gid) < 0)
			goto libnss_role_writing_exit;

		for(j = 0; j < G->gr[i].size; j++) {
			if (j)
				if (fprintf(f, ",") < 0)
					goto libnss_role_writing_exit;
			if (fprintf(f, "%u", G->gr[i].list[j]) < 0)
				goto libnss_role_writing_exit;
		}
		if (fprintf(f, "\n") < 0)
			goto libnss_role_writing_exit;
	}

	result = OK;

libnss_role_writing_exit:
	fclose(f);
	return result;
}