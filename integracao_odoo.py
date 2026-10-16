import xmlrpc.client
import json
import paho.mqtt.client as mqtt

# ==========================================
# 1. CREDENCIAIS DO ODOO
# ==========================================
URL = "https://tg-storage-management.odoo.com"
DB = "tg-storage-management"
USER = "teste@gmail.com"
PASSWORD = "MINHA_SENHA_AQUI" # Coloque a sua senha real aqui

# ==========================================
# 2. CONFIGURAÇÕES MQTT
# ==========================================
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
        
        if comando == "PEDIR_LISTA":
            print("\n[>] Cérebro solicitou a lista! Buscando o último pedido...")
            pickings = models.execute_kw(DB, uid, PASSWORD, 'stock.picking', 'search_read',
                [[['state', 'in', ['confirmed', 'assigned']]]],
                {'fields': ['name'], 'order': 'id desc', 'limit': 1})
            
            if not pickings:
                print("[!] Nenhuma ordem ativa encontrada no Odoo.")
                return
                
            picking_id = pickings[0]['id']
            print(f"    [+] Ordem encontrada: {pickings[0]['name']}")
            
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
            
            if lista_para_esp:
                client.publish(TOPICO_TX_PYTHON, json.dumps({"comando": "NOVA_LISTA", "produtos": lista_para_esp}))
                print(f"[<] Lista enviada para o ESP32!")

        elif comando == "SALVAR_ODOO":
            produtos = dados.get("produtos", [])
            print(f"\n[>] Cérebro finalizou! Processando {len(produtos)} produtos...")
            
            pickings = models.execute_kw(DB, uid, PASSWORD, 'stock.picking', 'search_read',
                [[['state', 'in', ['confirmed', 'assigned']]]],
                {'fields': ['name'], 'order': 'id desc', 'limit': 1})
                
            if not pickings:
                print("[!] Nenhum pedido ativo para atualizar.")
                return
                
            picking_id = pickings[0]['id']
            picking_name = pickings[0]['name']
            print(f"    [+] Atualizando estritamente o pedido: {picking_name}")
            
            html_relatorio = "<b>🤖 Relatório de Separação (Pick-to-Light ESP32):</b><br/>"
            
            for item in produtos:
                codigo = item['codigo']
                qtd_recebida_esp = float(item['qtd_separada'])
                status = item['status']
                
                # 1. BUSCA O MOVIMENTO E A QUANTIDADE INICIAL DEMANDADA
                moves = models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'search_read', 
                    [[['picking_id', '=', picking_id], ['product_id.default_code', '=', codigo]]],
                    {'fields': ['id', 'product_uom_qty']})
                    
                if moves:
                    move_id = moves[0]['id']
                    qtd_demanda_inicial = float(moves[0]['product_uom_qty'])
                    
                    # ====================================================================
                    # 2. REGRAS DE NEGÓCIO DE ESTOQUE E STATUS
                    # ====================================================================
                    if status == "Pendente" or status == "Prod Errado":
                        # Regra 1: Sempre zero.
                        qtd_final = 0.0
                        
                    elif status == "Falta Prod":
                        # Regra 2: Igual ou menor que o pedido inicial.
                        if qtd_recebida_esp > qtd_demanda_inicial:
                            qtd_final = qtd_demanda_inicial # Trava no valor máximo
                        else:
                            qtd_final = qtd_recebida_esp
                            
                    else: # Se for "Concluido"
                        qtd_final = qtd_recebida_esp
                    # ====================================================================
                    
                    # Formatação exata do seu código para a mensagem (usando o valor já corrigido)
                    if status == "Concluido": icone = "🟢"
                    elif status == "Pendente": icone = "⚪"
                    else: icone = "🔴"
                    
                    html_relatorio += f"{icone} <b>{codigo}</b>: Separou {int(qtd_final)} - <i>{status}</i><br/>"
                    
                    # 3. ATUALIZA AS TABELAS COM O VALOR FILTRADO
                    move_lines = models.execute_kw(DB, uid, PASSWORD, 'stock.move.line', 'search_read',
                        [[['move_id', '=', move_id]]], {'fields': ['id']})
                        
                    if move_lines:
                        for ml in move_lines:
                            ml_id = ml['id']
                            try: models.execute_kw(DB, uid, PASSWORD, 'stock.move.line', 'write', [[ml_id], {'quantity': qtd_final}])
                            except: pass
                            try: models.execute_kw(DB, uid, PASSWORD, 'stock.move.line', 'write', [[ml_id], {'qty_done': qtd_final}])
                            except: pass
                    else:
                        try: models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'write', [[move_id], {'quantity': qtd_final}])
                        except: pass
                        try: models.execute_kw(DB, uid, PASSWORD, 'stock.move', 'write', [[move_id], {'quantity_done': qtd_final}])
                        except: pass
                        
                    print(f" -> {codigo}: Recebeu {int(qtd_recebida_esp)} | Injetou no Odoo {int(qtd_final)} (Status: {status})")
            
            # ==== POSTA A MENSAGEM NO CHATTER ====
            try:
                models.execute_kw(DB, uid, PASSWORD, 'mail.message', 'create', [{
                    'model': 'stock.picking',
                    'res_id': picking_id, 
                    'body': html_relatorio,
                    'message_type': 'comment',
                    'subtype_id': 2 
                }])
                print(f"    [✓] Relatório visual anexado ao Odoo com sucesso!")
            except Exception as ex:
                print(f"    [!] Erro ao tentar postar a nota: {ex}")
                
            print("--- SINCRONIZAÇÃO FINALIZADA ---\n")
            
    except Exception as e:
        print(f"Erro interno no Python: {e}")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()