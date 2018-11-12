#include "signalling.h"

static GSList *eventQueue = NULL; // s_signalEventData

typedef struct {
    PurpleAccount *account;
    char *sender;
    char *buffer;
    PurpleConversation *conv;
    PurpleMessageFlags flags;
    void *data;
} s_EventDataImMessage;

typedef struct {
    PurpleAccount *account;
    char *description;
    PurpleConnectionError type;
} s_EventDataConnectionError;

typedef struct {
    PurpleAccount *account;
    char *sender;
    char *roomName;
    char *message;
    GHashTable *inviteProps;
} s_EventDataInvited;

napi_value getJsObjectForSignalEvent(napi_env env, s_signalEventData *eventData) {
    napi_value evtObj;
    napi_value value;
    napi_create_object(env, &evtObj);
    napi_create_string_utf8(env, eventData->signal, NAPI_AUTO_LENGTH, &value);
    napi_set_named_property(env, evtObj, "eventName", value);
    /* This is where things get a bit messy, we want to extract information about each event */
    if (
            strcmp(eventData->signal, "account-signed-on") == 0 ||
            strcmp(eventData->signal, "account-signed-off") == 0  ||
            strcmp(eventData->signal, "account-added") == 0  ||
            strcmp(eventData->signal, "account-removed") == 0  ||
            strcmp(eventData->signal, "account-connecting") == 0  ||
            strcmp(eventData->signal, "account-created") == 0  ||
            strcmp(eventData->signal, "account-destroying") == 0
            ) {
        PurpleAccount* prplAcct = (PurpleAccount*)eventData->data;
        napi_value acct = nprpl_account_create(env, prplAcct);
        napi_set_named_property(env, evtObj, "account", acct);
    }

    if (strcmp(eventData->signal, "account-connection-error") == 0) {
        s_EventDataConnectionError msgData = *(s_EventDataConnectionError*)eventData->data;
        napi_value acct = nprpl_account_create(env, msgData.account);
        napi_set_named_property(env, evtObj, "account", acct);

        napi_create_string_utf8(env, msgData.description, NAPI_AUTO_LENGTH, &value);
        napi_set_named_property(env, evtObj, "description", value);

        napi_create_uint32(env, msgData.type,&value);
        napi_set_named_property(env, evtObj, "type", value);
    }

    if (strcmp(eventData->signal, "received-im-msg") == 0 ||
        strcmp(eventData->signal, "received-chat-msg") == 0) {
        s_EventDataImMessage msgData = *(s_EventDataImMessage*)eventData->data;
        napi_value acct = nprpl_account_create(env, msgData.account);
        napi_set_named_property(env, evtObj, "account", acct);

        napi_create_string_utf8(env, msgData.buffer, NAPI_AUTO_LENGTH, &value);
        napi_set_named_property(env, evtObj, "message", value);

        napi_create_string_utf8(env, msgData.sender, NAPI_AUTO_LENGTH, &value);
        napi_set_named_property(env, evtObj, "sender", value);
        if (msgData.conv != NULL) {
            value = nprpl_conv_create(env, msgData.conv);
            napi_set_named_property(env, evtObj, "conv", value);
        }
    }

    if (strcmp(eventData->signal, "chat-invite") == 0) {
        s_EventDataInvited msgData = *(s_EventDataInvited*)eventData->data;
        napi_value acct = nprpl_account_create(env, msgData.account);
        napi_set_named_property(env, evtObj, "account", acct);
        if (msgData.message != NULL) {
            napi_create_string_utf8(env, msgData.message, NAPI_AUTO_LENGTH, &value);
            napi_set_named_property(env, evtObj, "message", value);
        }

        if (msgData.roomName != NULL) {
            napi_create_string_utf8(env, msgData.roomName, NAPI_AUTO_LENGTH, &value);
            napi_set_named_property(env, evtObj, "room_name", value);
        }

        if(msgData.sender != NULL) {
            napi_create_string_utf8(env, msgData.sender, NAPI_AUTO_LENGTH, &value);
            napi_set_named_property(env, evtObj, "sender", value);
        }

        napi_create_object(env, &value);
        GHashTableIter iter;
        gpointer key, val;
        napi_value jkey, jvalue;

        g_hash_table_iter_init (&iter, msgData.inviteProps);
        struct proto_chat_entry *pce;
        while (g_hash_table_iter_next (&iter, &key, &val))
        {
            char* skey = (char*)key;
            char* sval = (char*)val;
            napi_create_string_utf8(env, skey, NAPI_AUTO_LENGTH, &jkey);
            if (sval != NULL) {
                napi_create_string_utf8(env, sval, NAPI_AUTO_LENGTH, &jvalue);
            } else {
                napi_get_undefined(env, &jvalue);
            }
            napi_set_property(env, value, jkey, jvalue);

        }
        napi_set_named_property(env, evtObj, "join_properties", value);

    }
    return evtObj;
}

void signalling_push(s_signalEventData *eventData) {
    eventQueue = g_slist_append(eventQueue, eventData);
}

GSList *signalling_pop() {
    GSList* item = eventQueue;
    if (item != NULL) {
        eventQueue = g_slist_remove_link(eventQueue, item);
    }
    return item;
}

/* Handle specific callbacks below */

void handleReceivedMessage(PurpleAccount *account, char *sender, char *buffer, PurpleConversation *conv,
                             PurpleMessageFlags flags, void *data) {
    s_signalCbData cbData = *(s_signalCbData*)data;
    s_signalEventData *ev = malloc(sizeof(s_signalEventData));
    s_EventDataImMessage *msgData = malloc(sizeof(s_EventDataImMessage));

    ev->signal = cbData.signal;
    msgData->account = account;

    msgData->buffer = malloc(strlen(buffer));
    strcpy(msgData->buffer, buffer);

    msgData->sender = malloc(strlen(sender));
    strcpy(msgData->sender, sender);

    // TODO: Do not create a convo for chats
    // The first message won't have a conversation, so create it.
    if (conv == NULL) {
        // This line was stolen from server.c#L624 where immediately after emitting
        // received-im-msg it creates a conversation below.
        conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, sender);
    }

    PurpleConvChat* chat = purple_conversation_get_chat_data(conv);
    if (chat != NULL) {

    }

    msgData->conv = conv;
    msgData->flags = flags;
    ev->freeMe = true;
    ev->data = msgData;
    signalling_push(ev);
}

void handleInvited(PurpleAccount *account, const char *inviter, const char *room_name, const char *message, GHashTable *data) {
    s_signalEventData *ev = malloc(sizeof(s_signalEventData));
    s_EventDataInvited *msgData = malloc(sizeof(s_EventDataInvited));
    ev->signal = "chat-invite";
    msgData->account = account;


    if (inviter != NULL) {
        msgData->sender = malloc(strlen(inviter));
        strcpy(msgData->sender, inviter);
    } else {
        msgData->sender = NULL;
    }


    if (room_name != NULL) {
        msgData->roomName = malloc(strlen(room_name));
        strcpy(msgData->roomName, room_name);
    } else {
        msgData->roomName = NULL;
    }


    if (message != NULL) {
        msgData->message = malloc(strlen(message));
        strcpy(msgData->message, message);
    } else {
        msgData->message = NULL;
    }


    // XXX: I'm not sure this is the best way to copy the table
    //      however it seems the only reliable way to do it.

    msgData->inviteProps = g_hash_table_new(g_str_hash, g_str_equal);

    GHashTableIter iter;
    gpointer key, val;
    g_hash_table_iter_init (&iter, data);

    while (g_hash_table_iter_next (&iter, &key, &val))
    {
        g_hash_table_insert(msgData->inviteProps, key, val);
    }

    ev->freeMe = true;
    ev->data = msgData;
    signalling_push(ev);
}

void handleAccountConnectionError(PurpleAccount *account, PurpleConnectionError type, char* description) {
    s_signalEventData *ev = malloc(sizeof(s_signalEventData));
    s_EventDataConnectionError *msgData = malloc(sizeof(s_EventDataConnectionError));
    msgData->account = account;
    msgData->description = malloc(strlen(description));
    strcpy(msgData->description, description);
    msgData->type = type;
    ev->data = msgData;
    ev->signal = "account-connection-error";
    ev->freeMe = true;
    signalling_push(ev);
}
