import xmlrpc.client
import json
import paho.mqtt.client as mqtt

# 1. CREDENCIAIS DO ODOO (preencha os seus dados)

URL = "https://tg-storage-management.odoo.com"
DB = "tg-storage-management"
USER = "teste@gmail.com"
PASSWORD = "MINHA_SENHA_AQUI"


# 2. CONFIGURAÇÕES MQTT & MAPEAMENTO FISICO

MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
TOPICO_RX_PYTHON = "tg/rodrigo/odoo/tx"
TOPICO_TX_PYTHON = "tg/rodrigo/odoo/rx"

MAPA_CAIXAS = {
    "PRD-001": 4, "PRD-002": 3, "PRD-003": 1, "PRD-004": 2,
    "PRD-005": 2, "PRD-006": 3, "PRD-007": 4, "PRD-008": 1
}

print("Conectando ao Odoo...")
try:
    common = xmlrpc.client.ServerProxy(f'{URL}/xmlrpc/2/common')
    uid = common.authenticate(DB, USER, PASSWORD, {})
    models = xmlrpc.client.ServerProxy(f'{URL}/xmlrpc/2/object')
    print(f"Sucesso! Conectado ao Odoo (ID: {uid})")
except Exception as e:
    print(f"Erro ao conectar no Odoo: {e}")
    exit()

def on_connect(client, userdata, flags, rc):
    print("Conectado ao MQTT! Escutando o Cérebro do ESP32...")
    client.subscribe(TOPICO_RX_PYTHON)

def on_message(client, userdata, msg):
    payload = msg.payload.decode('utf-8')
    try:
        dados = json.loads(payload)
        comando = dados.get("comando")
        
        # FLUXO 1: ESP32 PEDE A LISTA
        if comando == "PEDIR_LISTA":
            print("\n[>] Cérebro solicitou a lista! Buscando o último pedido no Odoo...")
            pickings = models.execute_kw(DB, uid, PASSWORD, 'stock.picking', 'search_read',
                [[['state', 'in', ['confirmed', 'assigned']]]],
                {'fields': ['name'], 'order': 'id desc', 'limit': 1})
            
            if not pickings:
                print("[!] Nenhuma ordem de separação ativa encontrada no Odoo.")
                return
                
            picking_id = pickings[0]['id']
            print(f"    [+] Encontrou a Ordem: {pickings[0]['name']}")
            
            moves = models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'search_read',
                [[['picking_id', '=', picking_id]]], {'fields': ['product_id', 'product_uom_qty']})
            
            lista_para_esp = []
            for idx, move in enumerate(moves):
                prod_id = move['product_id'][0]
                prod_info = models.execute_kw(DB, uid, PASSWORD, 'product.product', 'read', [prod_id], {'fields': ['default_code', 'name']})[0]
                
                codigo = prod_info.get('default_code', 'SEM_COD')
                lista_para_esp.append({
                    "id_produto": idx + 1,
                    "caixa_alvo": MAPA_CAIXAS.get(codigo, 1),
                    "codigo": codigo,
                    "nome": prod_info.get('name', 'Sem Nome'),
                    "qtd_original": int(move['product_uom_qty'])
                })
            
            if len(lista_para_esp) > 0:
                client.publish(TOPICO_TX_PYTHON, json.dumps({"comando": "NOVA_LISTA", "produtos": lista_para_esp}))
                print(f"[<] Lista oficial enviada para o ESP32!")

        # FLUXO 2: ESP32 DEVOLVE OS DADOS PARA SALVAR NO ODOO
        elif comando == "SALVAR_ODOO":
            produtos = dados.get("produtos", [])
            print(f"\n[>] Cérebro finalizou! Atualizando TODOS os {len(produtos)} produtos no Odoo...")
            
            ids_dos_pedidos = set()
            html_relatorio = "<b>🤖 Relatório de Separação (Pick-to-Light ESP32):</b><br/>"
            
            for item in produtos:
                codigo = item['codigo']
                qtd = item['qtd_separada']
                status = item['status']
                
                if status == "Concluido": icone = "🟢"
                elif status == "Pendente": icone = "⚪"
                else: icone = "🔴"
                
                html_relatorio += f"{icone} <b>{codigo}</b>: Separou {qtd} - <i>{status}</i><br/>"
                
                moves = models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'search_read', 
                    [[['product_id.default_code', '=', codigo], ['state', 'in', ['confirmed', 'assigned']]]],
                    {'fields': ['picking_id']})
                    
                if moves:
                    id_move = moves[0]['id']
                    id_pedido = moves[0]['picking_id'][0]
                    ids_dos_pedidos.add(id_pedido)
                    
                    
                    try:
                        models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'write', [[id_move], {'quantity': qtd}])
                    except: pass
                    
                    try:
                        models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'write', [[id_move], {'quantity_done': qtd}])
                    except: pass
                    
                    print(f" -> {codigo}: Quantidade {qtd} salva no banco! (Status ESP32: {status})")
            
            
            for pedido_id in ids_dos_pedidos:
                try:
                    models.execute_kw(DB, uid, PASSWORD, 'mail.message', 'create', [{
                        'model': 'stock.picking',
                        'res_id': pedido_id,
                        'body': html_relatorio,
                        'message_type': 'comment',
                        'subtype_id': 2 # ID padrão para Log Note no Odoo
                    }])
                    print(f"    [✓] Relatório visual anexado ao Odoo com sucesso!")
                except Exception as ex:
                    print(f"    [!] Erro ao tentar postar a nota: {ex}")
                    
            print("--- SINCRONIZAÇÃO FINALIZADA ---\n")
            
    except Exception as e:
        print(f"Erro interno no Python: {e}")

# Inicia o MQTT e MANTÉM O SCRIPT RODANDO INFINITAMENTE
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()