#!/usr/bin/env python3
"""Volta os acentos nas strings da UI (as fontes antigas não tinham Latin-1).
Só mexe em literais com espaço ou em maiúsculas; ignora URLs, JSON e G-code."""
import re, sys
W = {
 'nao':'não','sao':'são','ja':'já','ha':'há','so':'só','ate':'até','sera':'será','tera':'terá','apos':'após',
 'tambem':'também','porem':'porém','voce':'você','tres':'três',
 'impressao':'impressão','impressoes':'impressões','conexao':'conexão','operacao':'operação','acao':'ação',
 'calibracao':'calibração','configuracao':'configuração','associacao':'associação','associacoes':'associações',
 'exaustao':'exaustão','circulacao':'circulação','execucao':'execução','versao':'versão','padrao':'padrão',
 'botao':'botão','atencao':'atenção','direcao':'direção','edicao':'edição','gravacao':'gravação',
 'verificacao':'verificação','insercao':'inserção','opcao':'opção','opcoes':'opções','posicao':'posição',
 'localizacao':'localização','observacao':'observação','selecao':'seleção','informacao':'informação',
 'informacoes':'informações','instrucao':'instrução','alteracao':'alteração','alteracoes':'alterações',
 'peca':'peça','pecas':'peças','cabecote':'cabeçote','cabecotes':'cabeçotes','armazem':'armazém',
 'diario':'diário','historico':'histórico','avancado':'avançado','avancadas':'avançadas','avancada':'avançada',
 'possivel':'possível','disponivel':'disponível','disponiveis':'disponíveis','indisponivel':'indisponível',
 'invalido':'inválido','invalida':'inválida','invalidos':'inválidos','valido':'válido','valida':'válida',
 'numero':'número','codigo':'código','memoria':'memória','area':'área','proximo':'próximo','proxima':'próxima',
 'minimo':'mínimo','maximo':'máximo','ultimo':'último','ultima':'última','ultimos':'últimos','pagina':'página',
 'obrigatorio':'obrigatório','obrigatoria':'obrigatória','diametro':'diâmetro','inicio':'início',
 'comentario':'comentário','usuario':'usuário','conteudo':'conteúdo','excluido':'excluído','excluida':'excluída',
 'concluida':'concluída','concluido':'concluído','fisico':'físico','fisica':'física','automatico':'automático',
 'automatica':'automática','proprio':'próprio','propria':'própria','necessario':'necessário','maquina':'máquina',
 'referencia':'referência','emergencia':'emergência','transferencia':'transferência','presenca':'presença',
 'endereco':'endereço','servico':'serviço','espaco':'espaço','metodo':'método','nivel':'nível',
 'rapido':'rápido','unico':'único','unica':'única','sequencia':'sequência','genero':'gênero',
 'vazia':'vazia','balanca':'balança','ate':'até','tamanho':'tamanho','codigos':'códigos','numeros':'números',
 'conexoes':'conexões','acoes':'ações','operacoes':'operações','faltando':'faltando','permissao':'permissão',
 'sessao':'sessão','funcao':'função','razao':'razão','duplicado':'duplicado','situacao':'situação',
 'cadastrao':'cadastrão','possiveis':'possíveis','periodo':'período','indice':'índice','unidade':'unidade',
 'rotulo':'rótulo','inicializacao':'inicialização','reinicio':'reinício','temporario':'temporário',
 'requisicao':'requisição','confirmacao':'confirmação','alimentacao':'alimentação','catalogo':'catálogo',
 'atualizacao':'atualização','conexao,':'conexão','descricao':'descrição','identificacao':'identificação','solicitacao':'solicitação','vinculo':'vínculo','varios':'vários','varias':'várias',
}
PHRASES = [('esta em andamento','está em andamento'),('esta livre','está livre'),('esta associada','está associada'),
           ('esta associado','está associado'),('nao esta','não está'),('Nao esta','Não está'),('esta vazio','está vazio'),
           ('esta vazia','está vazia'),('esta ocupad','está ocupad'),('esta conectad','está conectad'),
           ('ESTA TAG','ESTA TAG'),('esta offline','está offline'),('esta pronta','está pronta'),('esta em ','está em '),('esta arquivad','está arquivad'),('esta indispon','está indispon'),('ja esta','já está')]
SKIP = re.compile(r'http|/v1|/server|/printer|\{|SET_|M10|%20|request_method|application/')

def fix_word(m):
    w = m.group(0)
    lw = w.lower()
    if lw not in W: return w
    r = W[lw]
    if w.isupper() and len(w) > 1: return r.upper()
    if w[0].isupper(): return r[0].upper() + r[1:]
    return r

def fix_lit(lit):
    body = lit[1:-1]
    if SKIP.search(body): return lit
    if ' ' not in body and not (body.isupper() and len(body) > 3): return lit
    ph = body.replace('\\n', '\x01').replace('\\"', '\x02')
    for a, b in PHRASES: ph = ph.replace(a, b)
    ph = re.sub(r'[A-Za-z]+', fix_word, ph)
    return '"' + ph.replace('\x01', '\\n').replace('\x02', '\\"') + '"'

for path in sys.argv[1:]:
    src = open(path, encoding='utf-8').read()
    out = []
    # não mexe em comentários nem em #include
    tok = re.compile(r'//[^\n]*|/\*.*?\*/|#include[^\n]*|\'(?:[^\'\\]|\\.)*\'|"(?:[^"\\\n]|\\.)*"', re.S)
    pos = 0
    for m in tok.finditer(src):
        out.append(src[pos:m.start()])
        t = m.group(0)
        out.append(fix_lit(t) if t.startswith('"') else t)
        pos = m.end()
    out.append(src[pos:])
    new = ''.join(out)
    if new != src:
        open(path, 'w', encoding='utf-8').write(new)
        print('acentos:', path)
