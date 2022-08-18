#include "chibicc.h"

static FILE *output_file;
static int depth;
static char *argreg[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  println("  addi.d $sp, $sp, -8");
  println("  st.d $a0, $sp, 0");
  depth++;
}

static void pop(char *arg) {
  println("  ld.d $%s, $sp, 0", arg);
  println("  addi.d $sp, $sp, 8");
  depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local) {
      // Local variable
      println("  li.d $t1, %d", node->var->offset - node->var->ty->size);
      println("  add.d $a0, $fp, $t1");
    } else {
      // Global variable
      println("  la.local $a0, %s", node->var->name);
    }

    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    println("  addi.d $a0, $a0, %d", node->member->offset);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value from where a0 is pointing to.
static void load(Type *ty) {
  if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    // If it is an array, do not attempt to load a value to the
    // register because in general we can't load an entire array to a
    // register. As a result, the result of an evaluation of an array
    // becomes not the array itself but the address of the array.
    // This is where "array is automatically converted to a pointer to
    // the first element of the array in C" occurs.
    return;
  }

  char *suffix = ty->is_unsigned ? "u" : "";

  // When we load a char or a short value to a register, we always
  // extend them to the size of int, so we can assume the lower half of
  // a register always contains a valid value. The upper half of a
  // register for char, short and int may contain garbage. When we load
  // a long value to a register, it simply occupies the entire register.
  if (ty->size == 1)
     println("  ld.b%s $a0, $a0, 0", suffix);
  else if (ty->size == 2)
     println("  ld.h%s $a0, $a0, 0", suffix);
  else if (ty->size == 4)
    println("  ld.w%s $a0, $a0, 0", suffix);
  else
     println("  ld.d $a0, $a0, 0");
}

// Store a0 to an address that the stack top is pointing to.
static void store(Type *ty) {
  pop("a1");

  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (int i = 0; i < ty->size; i++) {
      println("  ld.b $a4, $a0, %d\n", i);
      println("  st.b $a4, $a1, %d\n", i);
    }
    return;
  }

  if (ty->size == 1)
     println("  st.b $a0, $a1, 0");
  else if (ty->size == 2)
     println("  st.h $a0, $a1, 0");
  else if (ty->size == 4)
    println("  st.w $a0, $a1, 0");
  else
     println("  st.d $a0, $a1, 0");
}

enum { I8, I16, I32, I64, U8, U16, U32, U64 };

static int getTypeId(Type *ty) {
  switch (ty->kind) {
  case TY_CHAR:
    return ty->is_unsigned ? U8 : I8;
  case TY_SHORT:
    return ty->is_unsigned ? U16 : I16;
  case TY_INT:
    return ty->is_unsigned ? U32 : I32;
  case TY_LONG:
    return ty->is_unsigned ? U64 : I64;
  }
  return U64;
}

// The table for type casts
static char i32i8[] = "  slli.w $a0, $a0, 24\n  srai.w $a0, $a0, 24";
static char i32u8[] = "  andi $a0, $a0, 0xff";
static char i32i16[] = "  slli.w $a0, $a0, 16\n  srai.w $a0, $a0, 16";
static char i32u16[] = "  slli.d $a0, $a0, 48\n  srli.d $a0, $a0, 48";

static char *cast_table[][10] = {
  // i8   i16     i32   i64     u8     u16     u32   u64
  {NULL,  NULL,   NULL, NULL, i32u8, i32u16, NULL, NULL}, // i8
  {i32i8, NULL,   NULL, NULL, i32u8, i32u16, NULL, NULL}, // i16
  {i32i8, i32i16, NULL, NULL, i32u8, i32u16, NULL, NULL}, // i32
  {i32i8, i32i16, NULL, NULL,   i32u8, i32u16, NULL, NULL},   // i64
  {i32i8, NULL,   NULL, NULL, NULL,  NULL,   NULL, NULL}, // u8
  {i32i8, i32i16, NULL, NULL, i32u8, NULL,   NULL, NULL}, // u16
  {i32i8, i32i16, NULL, NULL, i32u8, i32u16, NULL, NULL}, // u32
  {i32i8, i32i16, NULL, NULL,   i32u8, i32u16, NULL, NULL},   // u64
};

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    println("  sltu $a0, $r0, $a0");
    return;
  }

  int t1 = getTypeId(from);
  int t2 = getTypeId(to);
  if (cast_table[t1][t2])
    println(cast_table[t1][t2]);
}

// Generate code for a given node.
static void gen_expr(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);

  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
    println("  li.d $a0, %ld", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    println("  sub.d $a0, $r0, $a0");
    return;
  case ND_VAR:
  case ND_MEMBER:
    gen_addr(node);
    load(node->ty);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    store(node->ty);
    return;
  case ND_STMT_EXPR:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_MEMZERO: {
    int offset = node->var->offset;
    for (int i = 0; i < node->var->ty->size; i++) {
      offset -= sizeof(char);
      println("  li.d $t1, %d", offset);
      println("  add.d $t1, $t1, $fp");
      println("  st.b $r0, $t1, 0");
    }
    return;
  }
  case ND_COND: {
    int c = count();
    gen_expr(node->cond);
    println("  beqz $a0, .L.else.%d", c);
    gen_expr(node->then);
    println("  b .L.end.%d", c);
    println(".L.else.%d:", c);
    gen_expr(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    println("  sltui $a0, $a0, 1");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    println("  li.d $a2, -1");
    println("  xor $a0, $a0, $a2");
    return;
  case ND_LOGAND: {
    int c = count();
    gen_expr(node->lhs);
    println("  beqz $a0, .L.false.%d", c);
    gen_expr(node->rhs);
    println("  beqz $a0, .L.false.%d", c);
    println("  li.d $a0, 1");
    println("  b .L.end.%d", c);
    println(".L.false.%d:", c);
    println("  li.d $a0, 0");
    println(".L.end.%d:", c);
    return;
  }
  case ND_LOGOR: {
    int c = count();
    gen_expr(node->lhs);
    println("  bne $a0, $r0, .L.true.%d", c);
    gen_expr(node->rhs);
    println("  bne $a0, $r0, .L.true.%d", c);
    println("  li.d $a0, 0");
    println("  b .L.end.%d", c);
    println(".L.true.%d:", c);
    println("  li.d $a0, 1");
    println(".L.end.%d:", c);
    return;
  }
  case ND_FUNCALL: {
    int nargs = 0;
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
      push();
      nargs++;
    }

    for (int i = nargs - 1; i >= 0; i--)
      pop(argreg[i]);

    if (depth % 2 == 0) {
      println("  bl %s", node->funcname);
    } else {
      println("  addi.d $fp, $fp,-8");
      println("  bl %s", node->funcname);
      println("  addi.d $fp, $fp, 8");
    }

    // It looks like the most significant 48 or 56 bits in a0 may
    // contain garbage if a function return type is short or bool/char,
    // respectively. We clear the upper bits here.
    switch (node->ty->kind) {
    case TY_BOOL:
      println("  andi $a0, $a0, 0xff");
    case TY_CHAR:
      if (node->ty->is_unsigned) {
        println("  andi $a0, $a0, 0xff");
      } else {
        println("  slli.w $a0, $a0, 24");
        println("  srai.w $a0, $a0, 24");
      }
      return;
    case TY_SHORT:
      if (node->ty->is_unsigned) {
        println("  slli.d $a0, $a0, 48");
        println("  srli.d $a0, $a0, 48");
      } else {
        println("  slli.w $a0, $a0, 16");
        println("  srai.w $a0, $a0, 16");
      }
      return;
    }
    return;
  }
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("a1");

  char* suffix = node->lhs->ty->kind == TY_LONG || node->lhs->ty->base
               ? "d" : "w";
  switch (node->kind) {
  case ND_ADD:
    println("  add.%s $a0, $a0, $a1", suffix);
    return;
  case ND_SUB:
    println("  sub.%s $a0, $a0, $a1", suffix);
    return;
  case ND_MUL:
    println("  mul.%s $a0, $a0, $a1", suffix);
    return;
  case ND_DIV:
    if (node->ty->is_unsigned) {
      println("  div.%su $a0, $a0, $a1", suffix);
    } else {
      println("  div.%s $a0, $a0, $a1", suffix);
    }
    return;
  case ND_MOD:
    if (node->ty->is_unsigned) {
      println("  mod.%su $a0, $a0, $a1", suffix);
    } else {
      println("  mod.%s $a0, $a0, $a1", suffix);
    }
    return;
  case ND_BITAND:
    println("  and $a0, $a0, $a1");
    return;
  case ND_BITOR:
    println("  or $a0, $a0, $a1");
    return;
  case ND_BITXOR:
    println("  xor $a0, $a0, $a1");
    return;
  case ND_EQ:
    println("  sub.d $a0, $a0, $a1");
    println("  sltui $a0, $a0, 1");
    return;
  case ND_NE:
    println("  sub.d $a0, $a0, $a1");
    println("  slt $a0, $a0, $r0");
    return;
  case ND_LT:
    if (node->lhs->ty->is_unsigned) {
      println("  sltu $a0, $a0, $a1");
    } else {
      println("  slt $a0, $a0, $a1");
    }
    return;
  case ND_LE:
    if (node->lhs->ty->is_unsigned) {
      println("  sltu $a0, $a1, $a0");
    } else {
      println("  slt $a0, $a1, $a0");
    }
    println("  xori $a0, $a0, 1");
    return;
  case ND_SHL:
    println("  sll.%s $a0, $a0, $a1", suffix);
    return;
  case ND_SHR:
    if (node->lhs->ty->is_unsigned) {
      println("  srl.%s $a0, $a0, $a1", suffix);
    } else {
      println("  sra.%s $a0, $a0, $a1", suffix);
    }
    return;
  default:
    break;
  }

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    println("  beqz $a0, .L.else.%d", c);
    gen_stmt(node->then);
    println("  b .L.end.%d", c);
    println(".L.else.%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    println(".L.begin.%d:", c);
    if (node->cond) {
      gen_expr(node->cond);
      println("  beqz $a0,%s", node->brk_label);
    }
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    if (node->inc)
      gen_expr(node->inc);
    println("  b .L.begin.%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_DO: {
    int c = count();
    println(".L.begin.%d:", c);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    gen_expr(node->cond);
    println("  bne $a0, $r0, .L.begin.%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_SWITCH:
    gen_expr(node->cond);

    for (Node *n = node->case_next; n; n = n->case_next) {
      println("  li.d $a4, %ld", n->val);
      println("  beq $a0, $a4, %s", n->label);
    }

    if (node->default_case)
      println("  b %s", node->default_case->label);

    println("  b %s", node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->brk_label);
    return;
  case ND_CASE:
    println("%s:", node->label);
    gen_stmt(node->lhs);
    return;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_GOTO:
    println("  b %s", node->unique_label);
    return;
  case ND_LABEL:
    println("%s:", node->unique_label);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs)
      gen_expr(node->lhs);
    println("  b .L.return.%s", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    int offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      offset = align_to(offset, var->align);
      var->offset = -offset;
      offset += var->ty->size;
    }
    fn->stack_size = align_to(offset, 16);
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function || !var->is_definition)
      continue;

    if (var->is_static)
      println("  .local %s", var->name);
    else
      println("  .globl %s", var->name);

    println("  .align %d", (int)log2(var->align));

    if (var->init_data) {
      println("  .data");
      println("%s:", var->name);

      Relocation *rel = var->rel;
      int pos = 0;
      while (pos < var->ty->size) {
        if (rel && rel->offset == pos) {
          println("  .quad %s%+ld", rel->label, rel->addend);
          rel = rel->next;
          pos += 8;
        } else {
          println("  .byte %d", var->init_data[pos++]);
        }
      }
      continue;
    }

    println("  .bss");
    println("%s:", var->name);
    println("  .zero %d", var->ty->size);
  }
}

static void store_gp(int r, int offset, int sz) {
  println("  li.d $t1, %d", offset - sz);
  println("  add.d $t1, $t1, $fp");
  switch (sz) {
  case 1:
    println("  st.b $%s, $t1, 0", argreg[r]);
    return;
  case 2:
    println("  st.h $%s, $t1, 0", argreg[r]);
    return;
  case 4:
    println("  st.w $%s, $t1, 0", argreg[r]);
    return;
  case 8:
    println("  st.d $%s, $t1, 0", argreg[r]);
    return;
  }
  unreachable();
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;

    if (fn->is_static)
      println("  .local %s", fn->name);
    else
      println("  .globl %s", fn->name);

    println("  .text");
    println("%s:", fn->name);
    current_fn = fn;

    // Prologue
    println("  st.d $ra, $sp, -8");
    println("  st.d $fp, $sp, -16");
    println("  addi.d $fp, $sp, -16");
    println("  li.d $t1, -%d", fn->stack_size + 16);
    println("  add.d $sp, $sp, $t1");

//    println("  addi.d $sp, $sp, -%d", fn->stack_size);

    // Save passed-by-register arguments to the stack
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      // __va_area__
      if (var->ty->kind == TY_ARRAY) {
        int offset = var->offset - var->ty->size;
        while (i < 8) {
          offset += 8;
          store_gp(i++, offset, 8);
        }
      } else {
        store_gp(i++, var->offset, var->ty->size);
      }
    }

    // Emit code
    gen_stmt(fn->body);
    assert(depth == 0);

    // Epilogue
    println(".L.return.%s:", fn->name);
    println("  li.d $t1, %d", fn->stack_size + 16);
    println("  add.d $sp, $sp, $t1");
    println("  ld.d $ra, $sp, -8");
    println("  ld.d $fp, $sp, -16");
    println("  jr $ra");
  }
}

void codegen(Obj *prog, FILE *out) {
  output_file = out;

  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);

  println(".LFE0:");
  println("  .size   main, .-main");
  println("  .section  .note.GNU-stack,\"\",@progbits");
}
