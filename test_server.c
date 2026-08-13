#include <open62541.h>
#include <signal.h>
#include <stdio.h>
#include <math.h>

static volatile UA_Boolean running = true;
static void stopHandler(int sig) { (void)sig; running = false; }

static UA_NodeId tempNodeId;
static UA_NodeId counterNodeId;

static void updateCallback(UA_Server *server, void *data)
{
    (void)data;
    static float phase = 0.0f;
    static int counter = 0;
    phase += 0.1f;
    counter++;

    float temp = 25.0f + 5.0f * sinf(phase);
    UA_Variant tVar;
    UA_Variant_init(&tVar);
    UA_Variant_setScalar(&tVar, &temp, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, tempNodeId, tVar);

    int c = counter;
    UA_Variant cVar;
    UA_Variant_init(&cVar);
    UA_Variant_setScalar(&cVar, &c, &UA_TYPES[UA_TYPES_INT32]);
    UA_Server_writeValue(server, counterNodeId, cVar);
}

int main(void)
{
    signal(SIGINT, stopHandler);

    UA_Server *server = UA_Server_new();
    UA_ServerConfig_setDefault(UA_Server_getConfig(server));

    // ✅ استفاده از Namespace 1 (همیشه معتبر است)
    // Temperature (float) -> ns=1;s=Temperature
    UA_VariableAttributes tAttr = UA_VariableAttributes_default;
    tAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Temperature");
    tAttr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    tAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    float temp = 25.0f;
    UA_Variant_setScalar(&tAttr.value, &temp, &UA_TYPES[UA_TYPES_FLOAT]);
    tempNodeId = UA_NODEID_STRING(1, "Temperature");
    UA_StatusCode retval = UA_Server_addVariableNode(server, tempNodeId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, "Temperature"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        tAttr, NULL, NULL);
    printf("Add Temperature node: %s\n", UA_StatusCode_name(retval));

    // Counter (int32) -> ns=1;s=Counter
    UA_VariableAttributes cAttr = UA_VariableAttributes_default;
    cAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Counter");
    cAttr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    cAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    int counter = 0;
    UA_Variant_setScalar(&cAttr.value, &counter, &UA_TYPES[UA_TYPES_INT32]);
    counterNodeId = UA_NODEID_STRING(1, "Counter");
    retval = UA_Server_addVariableNode(server, counterNodeId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, "Counter"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        cAttr, NULL, NULL);
    printf("Add Counter node: %s\n", UA_StatusCode_name(retval));

    UA_Server_addRepeatedCallback(server, updateCallback, NULL, 500.0, NULL);

    printf("Test OPC UA server on opc.tcp://0.0.0.0:4840 (Ctrl+C to stop)\n");
    UA_Server_run(server, &running);
    UA_Server_delete(server);
    return 0;
}