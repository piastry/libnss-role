#include <role/parser.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int librole_graph_add(struct librole_graph *G, struct librole_ver v)
{
	if (G->size == G->capacity) {
		G->capacity <<= 1;
		G->gr = (struct librole_ver *) realloc(G->gr, sizeof(struct librole_ver) * G->capacity);
		if (!G->gr)
			return LIBROLE_MEMORY_ERROR;
		G->used = (int *) malloc(sizeof(int) * G->capacity);
		if (!G->used) {
			free(G->gr);
			return LIBROLE_MEMORY_ERROR;
		}
	}
	G->gr[G->size++] = v;
	return LIBROLE_OK;
}

int librole_ver_add(struct librole_ver *v, gid_t g)
{
	if (v->size == v->capacity) {
		v->capacity <<= 1;
		v->list = (gid_t *) realloc(v->list, sizeof(gid_t) * v->capacity);
		if (!v->list)
			return LIBROLE_MEMORY_ERROR;
	}
	v->list[v->size++] = g;
	return LIBROLE_OK;
}

int librole_graph_init(struct librole_graph *G)
{
	G->gr = (struct librole_ver *) malloc(sizeof(struct librole_ver) * G->capacity);
	if (!G->gr)
		return LIBROLE_MEMORY_ERROR;

	G->used = (int *) malloc(sizeof(int) * G->capacity);
	if (!G->used) {
		free(G->gr);
		return LIBROLE_MEMORY_ERROR;
	}

	return LIBROLE_OK;
}

int librole_ver_init(struct librole_ver *v)
{
	v->list = (gid_t *) malloc(sizeof(gid_t) * v->capacity);
	if (!v->list)
		return LIBROLE_MEMORY_ERROR;

	return LIBROLE_OK;
}

int librole_find_id(struct librole_graph *G, gid_t g, int *id)
{
	int i;

	for(i = 0; i < G->size; i++) {
		if (G->gr[i].gid == g) {
			*id = i;
			return LIBROLE_OK;
		}
	}

	return LIBROLE_NO_SUCH_GROUP;
}

void librole_free_all(struct librole_graph *G)
{
	int i;

	for(i = 0; i < G->size; i++)
		free(G->gr[i].list);

	free(G->gr);
	free(G->used);
}

int librole_realloc_groups(long int **size, gid_t ***groups, long int new_size)
{
	gid_t *new_groups;

	new_groups = (gid_t *)
		realloc((**groups),
			new_size * sizeof(***groups));

	if (!new_groups)
		return LIBROLE_MEMORY_ERROR;

	**groups = new_groups;
	**size = new_size;

	return LIBROLE_OK;
}

/* return 1 if finished on the start of the comment, otherwise - return 0 */
static int select_line_part(char *line, unsigned long len, char **last,
			     unsigned long *pos, const char symbol)
{
	unsigned long i = *pos;
	int in_progress = 0;
	int rc = 0;

	for(; i < len; i++) {
		if (line[i] == ' ' && !in_progress) {
			(*last)++;
			continue;
		} else if (line[i] != ' ' && !in_progress)
			in_progress = 1;

		if (line[i] == '#') {
			line[i++] = '\0';
			rc = 1;
			break;
		}

		if (line[i] == symbol) {
			line[i++] = '\0';
			break;
		}
	}

	if (i > 1) {
		/* we have at least one symbol for a role name */
		unsigned long j = i - 1;

		while (j >= 1 && line[j - 1] == ' ')
			line[--j] = '\0';
	}

	*pos = i;
	return rc;
}

static int parse_line(char *line, struct librole_graph *G)
{
	int result;
	unsigned long len = strlen(line);
	unsigned long i = 0;
	char *last = line;
	struct librole_ver role = {0, 0, 0, 10};
	int comment = 0;

	/* skip blank line */
	if (!len)
		return LIBROLE_OK;

	result = librole_ver_init(&role);
	if (result != LIBROLE_OK)
		return result;

	comment = select_line_part(line, len, &last, &i, ':');

	if (comment && *last == '\0')
		return result;

	result = librole_get_gid(last, &role.gid);
	if (result != LIBROLE_OK)
		goto libnss_role_parse_line_error;

	while (!comment) {
		if (i >= len)
			break;
		last = line + i;
		gid_t gr;

		comment = select_line_part(line, len, &last, &i, ',');

		result = librole_get_gid(last, &gr);
		if (result != LIBROLE_OK && result != LIBROLE_NO_SUCH_GROUP)
			goto libnss_role_parse_line_error;
		else if (result == LIBROLE_NO_SUCH_GROUP)
			continue;

		result = librole_ver_add(&role, gr);
		if (result != LIBROLE_OK)
			goto libnss_role_parse_line_error;
	}

	result = librole_graph_add(G, role);
	if (result != LIBROLE_OK)
		goto libnss_role_parse_line_error;
	return result;

libnss_role_parse_line_error:
	free(role.list);
	return result;
}

int librole_reading(const char *s, struct librole_graph *G)
{
	int result = LIBROLE_OK;
	FILE *f = NULL;
	unsigned long len = LIBROLE_STR_MIN_SIZE;
	char *str = NULL;
	unsigned long id = 0;
	char c;

	f = fopen(s, "r");
	if (!f)
		return LIBROLE_IO_ERROR;

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
			if (result != LIBROLE_OK &&
					result != LIBROLE_NO_SUCH_GROUP)
				goto libnss_role_reading_out;
			id = 0;
			result = LIBROLE_OK;
			continue;
		}
		str[id++] = c;
		if (id == len) {
			len <<= 1;
			str = realloc(str, len * sizeof(char));
			if (!str) {
				result = LIBROLE_MEMORY_ERROR;
				goto libnss_role_reading_out;
			}
		}
	}
	if (id) {
		str[id] = '\0';
		result = parse_line(str, G);
		if (result != LIBROLE_OK)
			goto libnss_role_reading_out;
	}
	
libnss_role_reading_out:
	free(str);
libnss_role_reading_close:
	fclose(f);
	return result;
}

int librole_dfs(struct librole_graph *G, gid_t v, librole_group_collector *col)
{
	int i, j, result;

	result = librole_find_id(G, v, &i);
	if (result != LIBROLE_OK) {
		result = librole_ver_add(col, v);
		return result;
	}

	if (G->used[i])
		return LIBROLE_OK;
	result = librole_ver_add(col, v);
	if (result != LIBROLE_OK)
		return result;
	G->used[i] = 1;

	for(j = 0; j < G->gr[i].size; j++) {
		result = librole_dfs(G, G->gr[i].list[j], col);
		if (result != LIBROLE_OK && result != LIBROLE_NO_SUCH_GROUP)
			return result;
	}
	return LIBROLE_OK;
}

int librole_get_gid(char *gr_name, gid_t *ans)
{
	unsigned long namelen = strlen(gr_name);
	struct group grp, *grp_ptr;
	gid_t gid;
	char buffer[1000];

	if (!namelen)
		return LIBROLE_IO_ERROR;

	if ((gr_name[0] != '"' && gr_name[namelen-1] == '"') ||
	    (gr_name[0] == '"' && gr_name[namelen-1] != '"'))
		return LIBROLE_IO_ERROR;

	if (gr_name[0] == '"' && gr_name[namelen-1] == '"') {
		gr_name[namelen-1] = '\0';
		gr_name++;
	} else {
		char *p;
		gid = (gid_t)strtoul(gr_name, &p, 10);
		if (*p == '\0' && (gid != 0 || gr_name[0] == '0')) {
			if (getgrgid_r(gid, &grp, buffer,
				       1000, &grp_ptr) != 0) {
				if (errno == ERANGE)
					return LIBROLE_OUT_OF_RANGE;
				if (errno != 0)
					return LIBROLE_UNKNOWN_ERROR;
			}

			if (!grp_ptr)
				return LIBROLE_NO_SUCH_GROUP;

			*ans = gid;
			return LIBROLE_OK;
		}
	}

	if (getgrnam_r(gr_name, &grp, buffer, 1000, &grp_ptr) != 0) {
		if (errno == ERANGE)
			return LIBROLE_OUT_OF_RANGE;
		if (errno != 0)
			return LIBROLE_UNKNOWN_ERROR;
	}

	if (!grp_ptr)
		return LIBROLE_NO_SUCH_GROUP;

	*ans = grp.gr_gid;
	return LIBROLE_OK;
}

int librole_writing(const char *file, struct librole_graph *G)
{
	int i, j, result = LIBROLE_IO_ERROR;
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

	result = LIBROLE_OK;

libnss_role_writing_exit:
	fclose(f);
	return result;
}
