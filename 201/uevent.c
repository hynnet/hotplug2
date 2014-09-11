#include "uevent.h"

/**
 * Trivial function that takes a seqnum string as input
 * and provides seqnum through second argument.
 *
 * @1 String form of seqnum
 * @2 Returned seqnum as number
 *
 * Returns: -1 if failure, 0 if success
 *
 */
static int set_seqnum(const char *seqnum_str, event_seqnum_t *seqnum) {
	if (seqnum_str == NULL)
		return -1;
	
	*seqnum = strtoull(seqnum_str, NULL, 0);
	return 0;
}

/**
 * A trivial function determining the action that the uevent.
 *
 * @1 String containing the action name (null-terminated).
 *
 * Returns: Macro of the given action
 */
static inline int get_uevent_action(char *action) {
	if (!strcmp(action, "add"))
		return ACTION_ADD;
	
	if (!strcmp(action, "remove"))
		return ACTION_REMOVE;
	
	return ACTION_UNKNOWN;
}

/**
 * Release all memory associated with an uevent read from kernel. The given
 * pointer is no longer valid, as it gets freed as well.
 *
 * @1 The uevent that is to be freed.
 *
 * Returns: void
 */
void uevent_free(struct uevent_t *uevent) {
	int i;
	
	for (i = 0; i < uevent->env_vars_c; i++) {
		free(uevent->env_vars[i].key);
		free(uevent->env_vars[i].value);
	}
	free(uevent->env_vars);
	free(uevent->action_str);
	free(uevent->plain);
	free(uevent);
}

/**
 * Looks up a value according to the given key.
 *
 * @1 A hotplug uevent structure
 * @2 Key for lookup
 *
 * Returns: The value of the key or NULL if no such key found
 */
char *uevent_getvalue(const struct uevent_t *uevent, const char *key) {
	int i;
	
	for (i = 0; i < uevent->env_vars_c; i++) {
		if (!strcmp(uevent->env_vars[i].key, key))
			return uevent->env_vars[i].value;
	}

	return NULL;
}

/**
 * Appends a key-value pair described by the second argument to the
 * hotplug uevent.
 *
 * @1 A hotplug uevent structure
 * @2 An item in format "key=value" to be appended
 *
 * Returns: 0 if success, -1 if the string is malformed
 */
int uevent_add_env(struct uevent_t *uevent, const char *item) {
	char *ptr, *tmp;
	
	ptr = strchr(item, '=');
	if (ptr == NULL)
		return -1;
	
	*ptr='\0';
	
	uevent->env_vars_c++;
	uevent->env_vars = xrealloc(uevent->env_vars, sizeof(struct env_var_t) * uevent->env_vars_c);
	uevent->env_vars[uevent->env_vars_c - 1].key = strdup(item);
	uevent->env_vars[uevent->env_vars_c - 1].value = strdup(ptr + 1);
	
	/*
	 * Variables not generated by kernel but demanded nonetheless...
	 *
	 * TODO: Split this to a different function
	 */
	if (!strcmp(item, "DEVPATH")) {
		uevent->env_vars_c++;
		uevent->env_vars = xrealloc(uevent->env_vars, sizeof(struct env_var_t) * uevent->env_vars_c);
		uevent->env_vars[uevent->env_vars_c - 1].key = strdup("DEVICENAME");
		tmp = strdup(ptr + 1);
		uevent->env_vars[uevent->env_vars_c - 1].value = strdup(basename(tmp));
		free(tmp);
	}
	
	*ptr='=';
	
	return 0;
}

/**
 * Duplicates all allocated memory of a source hotplug uevent
 * and returns a new hotplug uevent, an identical copy of the
 * source uevent.
 *
 * @1 Source hotplug uevent structure
 *
 * Returns: A copy of the source uevent structure
 */
struct uevent_t *uevent_dup(const struct uevent_t *src) {
	struct uevent_t *dest;
	int i;
	
	dest = xmalloc(sizeof(struct uevent_t));
	dest->action = src->action;
	dest->env_vars_c = src->env_vars_c;
	dest->env_vars = xmalloc(sizeof(struct env_var_t) * dest->env_vars_c);
	dest->plain_s = src->plain_s;
	dest->plain = xmalloc(dest->plain_s);
	memcpy(dest->plain, src->plain, dest->plain_s);
	
	for (i = 0; i < src->env_vars_c; i++) {
		dest->env_vars[i].key = strdup(src->env_vars[i].key);
		dest->env_vars[i].value = strdup(src->env_vars[i].value);
	}
	
	return dest;
}

/**
 * Parses a string into a hotplug uevent structurs.
 *
 * @1 The uevent string (not null terminated)
 * @2 The size of the uevent string
 *
 * Returns: A new uevent structure
 */
struct uevent_t *uevent_deserialize(char *uevent_str, int size) {
	char *ptr;
	struct uevent_t *uevent;
	int skip;
	
	ptr = strchr(uevent_str, '@');
	if (ptr == NULL) {
		return NULL;
	}
	*ptr='\0';
	
	uevent = xmalloc(sizeof(struct uevent_t));
	uevent->action_str = strdup(uevent_str);
	uevent->action = get_uevent_action(uevent_str);
	uevent->env_vars_c = 0;
	uevent->env_vars = NULL;
	uevent->plain_s = size;
	uevent->plain = xmalloc(size);

	*ptr='@';
	memcpy(uevent->plain, uevent_str, size);
	*ptr='\0';
	
	skip = ++ptr - uevent_str;
	size -= skip;
	
	while (size > 0) {
		uevent_add_env(uevent, ptr);
		skip = strlen(ptr);
		ptr += skip + 1;
		size -= skip + 1;
	}

	/* We need seqnum to prevent race with kernel. */
	if (set_seqnum(uevent_getvalue(uevent, "SEQNUM"), &uevent->seqnum) == -1) {
		uevent_free(uevent);
		return NULL;
	}
	
	return uevent;
}
