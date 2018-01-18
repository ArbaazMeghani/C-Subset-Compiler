//import statements
#include "ast.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>


//global variables
extern FILE *outfile;
extern AstNode *program;
AstNode *currentMethod;
int labelCounter;

//emit label
void emit_label(const char *label){
	fprintf(outfile, "%s:\n", label); 
}

//emit instruction
void emit(const char *instr){
	fprintf(outfile, "\t%s\n", instr);
}

//create a new label
char *gen_new_label(){
	//generate label based on label counter
	char *label = malloc(sizeof(char)*128);
	snprintf(label, 128, "L%d", labelCounter);
	labelCounter++;
	return label;
}

//function declaration for getting variable offset
int getVarOffset(AstNode *node);

/*
*	Function: generate code for local variables
*	Parameters: none
*	Return: none
*/
void code_gen_var() {
	int offset = getVarOffset(currentMethod);
	char str[512];
	offset = -1*(offset+4);
	if(offset == 0)
		return;
	snprintf(str, 512, "subu $sp, $sp, %d", offset);
	emit(str);
}

/*
*	Function: count number of local variables
*	Parameters: none
*	Return: number of local variables
*/
int countVars() {
	int i;
	int count = 0;
	for(i = 0; i < MAXHASHSIZE; i++) {
		ElementPtr element = currentMethod->children[1]->nSymbolTabPtr->hashTable[i];
		if(element != NULL && element->stype != NULL) {
			count++;
		}
	}
	return count;
}

/*
*	Function: comparator function to sort var list
*	Parameters: 2 elements to be compared
*	Return: 0 if on same line, 1 if in correct order, -1 otherwise
*/
int compar(const void *a, const void *b) {
	ElementPtr elemA = *(ElementPtr *)a;
	ElementPtr elemB = *(ElementPtr *)b;
	int lineA = elemA->linenumber;
	int lineB = elemB->linenumber;
	return (lineA > lineB) - (lineA < lineB);
}

/*
*	Function: build an array of all local variables
*	Parameters: number of variables
*	Return: array of elementptrs
*/
ElementPtr *buildVarList(int count) {
	ElementPtr *varList = (ElementPtr *)malloc(sizeof(ElementPtr)*count);
	int i;
	int j = 0;
	for(i = 0; i < MAXHASHSIZE; i++) {
		ElementPtr element = currentMethod->children[1]->nSymbolTabPtr->hashTable[i];
		if(element != NULL && element->stype != NULL) {
			varList[j] = element;
			j++;
		}
	}
	qsort(varList, count, sizeof(ElementPtr), compar);
	return varList;
}

/*
*	Function: check if local variable exists
*	Parameters: variable node, list of variables, count of variables
*	Return: 1 if exist; 0 otherwise
*/
int var_exists(AstNode *node, ElementPtr *varList, int count) {
	int i = 0;
	for(i = 0; i < count; i++)
		if(strcmp(varList[i]->id, node->nSymbolPtr->id) == 0)
			return 1;
	return 0;
}

int getArgOffset(AstNode *node) {
	return 0;
}

/*
*	Function: get the offset of variable from frame pointer
*	Parameters: the variable node
*	Return: the offset
*/
int getVarOffset(AstNode *node) {
	int count = countVars();
	ElementPtr *varList = buildVarList(count);
	/*if(!var_exists(node, varList, count)) {
		return getArgOffset(node);
	}*/
	int offset = 1;
	int i;
	for(i = 0; i < count; i++) {
		switch(varList[i]->stype->kind) {
			case INT:
				offset++;
				break;
			case ARRAY:
				offset += varList[i]->stype->dimension;
				break;
			default:break;
		}
		if(strcmp(varList[i]->id, node->nSymbolPtr->id) == 0)
			return -4*offset;
	}
	return -4*offset;
}

/*
*	Function: push a register to the stack
*	Parameters: the register name
*	Return: none
*/
void push_reg_to_stack(char *reg) {
	emit("subu $sp, $sp, 4");
	char str[512];
	snprintf(str, 512, "sw %s, 0($sp)", reg);
	emit(str);
}

/*
*	Functino: pop register from stack
*	Paraemters: the register to put value in
*	Return: none
*/
void pop_stack_to_reg(char *reg) {
	char str[512];
	snprintf(str, 512, "lw %s, 0($sp)", reg);
	emit(str);
	emit("addiu $sp, $sp, 4");
}

/*
*	Function: handle array expresions
*	Parameters: node
*	Return: none
*/
void array_exp_handler(AstNode *node) {
	int varOffset = getVarOffset(node);
	code_gen_expr(node->children[0]);
	varOffset += 4*(node->nSymbolPtr->stype->dimension - 1);
	char str[512];
	emit("li $v1, -4");
	emit("mul $v0, $v0, $v1");
	snprintf(str, 512, "addiu $v0, $v0, %d", varOffset);
	emit(str);
}

/*
*	Function: generate code for lvalue
*	Parameters: lvalue node
*	Return: none
*/
void code_gen_expr_lvalue(AstNode *node) {
	switch(node->eKind) {
		case VAR_EXP: {
			int offset = getVarOffset(node);
			char str[512];
			snprintf(str, 512, "sw $v0, %d($fp)", offset);
			emit(str);
			break;
		}
		case ARRAY_EXP:
			push_reg_to_stack("$v0");
			array_exp_handler(node);
			pop_stack_to_reg("$v1");
			emit("addu $v0, $v0 $fp");
			emit("sw $v1, 0($v0)");
			break;
		default: break;
	}
}

/*
*	Function: generate code for binary expression
*	Parameters: node
*	Return : none
*/
void code_gen_binary_expr(AstNode *node) {
	code_gen_expr(node->children[0]);
	push_reg_to_stack("$v0");
	code_gen_expr(node->children[1]);
	pop_stack_to_reg("$v1");
}

/*
*	Function: restore stack after function call
*	Parameters: node
*	Return: none
*/
void restore_stack(AstNode *node) {
	emit("lw $ra -4($fp)");
	emit("lw $fp, 0($fp)");
	int offset = getVarOffset(node) + 4;
	if(offset < 0 ) {
		offset *= -1;
		char str[512];
		snprintf(str, 512, "addiu $sp, $sp, %d", offset);
		emit(str);
	}
	emit("addiu $sp, $sp, 8");
}

/*
*	Function: handle output system call
*	Parameters: node
*	Return: none
*/
void output(AstNode *node) {
	code_gen_expr(node->children[0]);
	emit("addiu $a0, $v0, 0");
	emit("li $v0, 1");
	emit("syscall");
	//restore_stack();
}

/*
*	Function: handle input system call
*	Parameters: node
*	Return: none
*/
void input(AstNode *node) {
	emit("li $v0, 5");
	emit("syscall");
	//restore_stack();
}

/*
*	Function: push arguments of methodcall onto stack
*	Parameters: node
*	Return: none
*/
void code_gen_push_args(AstNode *node) {
	AstNode *args = node->children[0];
	if(args == NULL)
		return;
	while(args != NULL) {
		code_gen_expr(args);
		push_reg_to_stack("$v0");
		args = args->sibling;
	}	
}

/*
*	Function: pop arguments from stack
*	Parameters: node
*	Return: none
*/
void code_gen_pop_args(AstNode *node) {
	AstNode *args = node->children[0];
	if(args == NULL)
		return;
	while(args != NULL) {
		pop_stack_to_reg("$v1");
		args = args->sibling;
	}
}

/*
*	Function: handle method calls
*	Parameters: node
*	Return: none
*/
void method_call(AstNode *node) {
	ElementPtr method = node->nSymbolPtr;
	code_gen_push_args(node);
	char str[512];
	snprintf(str, 512, "jal %s", node->nSymbolPtr->id);
	emit(str);
	restore_stack(node);
	code_gen_pop_args(node);
}

void code_gen_expr(AstNode *node){
	if(node == NULL)
		return;
	switch(node->eKind) {
		case CONST_EXP: {
			char str[512];
			snprintf(str, 512, "li $v0, %d", node->nValue);
			emit(str);
			break;
		}
		case ASSI_EXP:
			code_gen_expr(node->children[1]);
			code_gen_expr_lvalue(node->children[0]);
			break;
		case VAR_EXP: {
			int varOffset = getVarOffset(node);
			char str[512];
			snprintf(str, 512, "lw $v0, %d($fp)", varOffset);
			emit(str);
			break;
		}
		case ARRAY_EXP: {
			array_exp_handler(node);
			emit("addu $v0, $v0, $fp");
			emit("lw $v0, 0($v0)");
			break;
		}
		case ADD_EXP:
			code_gen_binary_expr(node);
			emit("addu $v0, $v0, $v1");
			break;
		case SUB_EXP:
			code_gen_binary_expr(node);
			emit("subu $v0, $v1, $v0");
			break;
		case MULT_EXP:
			code_gen_binary_expr(node);
			emit("mul $v0, $v0, $v1");
			break;
		case DIV_EXP:
			code_gen_binary_expr(node);
			emit("divu $v0, $v1, $v0");
			break;
		case GT_EXP:
			code_gen_binary_expr(node);
			emit("sgt $v0, $v1, $v0");
			break;
		case GE_EXP:
			code_gen_binary_expr(node);
			emit("sge $v0, $v1, $v0");
			break;
		case LT_EXP:
			code_gen_binary_expr(node);
			emit("slt $v0, $v1, $v0");
			break;
		case LE_EXP:
			code_gen_binary_expr(node);
			emit("sle $v0, $v1, $v0");
			break;
		case EQ_EXP:
			code_gen_binary_expr(node);
			emit("seq $v0, $v0, $v1");
			break;
		case NE_EXP:
			code_gen_binary_expr(node);
			emit("sne $v0, $v0, $v1");
			break;
		case CALL_EXP: {
			if(strcmp(node->fname, "output") == 0)
				output(node);
			else if(strcmp(node->fname, "input") == 0)
				input(node);
			else
				method_call(node);
			break;
		}
		default:break;
	}
}

/*
*	Function: generate code for if statement
*	Parameters: node
*	Return: none
*/
void code_gen_if_stmt(AstNode *node) {
	char *endLabel = gen_new_label();
	code_gen_expr(node->children[0]);
	char str[512];
	snprintf(str, 512, "beqz $v0, %s", endLabel);
	emit(str);
	code_gen_stmt(node->children[1]);
	emit_label(endLabel);
	free(endLabel);
}

/*
*	Function: generate code for if else statement
*	Parameters: node
*	Return: none
*/
void code_gen_if_else_stmt(AstNode *node) {
	char *elseLabel = gen_new_label();
	char *endLabel = gen_new_label();
	code_gen_expr(node->children[0]);
	char str[512];
	snprintf(str, 512, "beqz $v0, %s", elseLabel);
	emit(str);
	code_gen_stmt(node->children[1]);
	snprintf(str, 512, "b %s", endLabel);
	emit(str);
	emit_label(elseLabel);
	code_gen_stmt(node->children[2]);
	emit_label(endLabel);
	free(elseLabel);
	free(endLabel);
}

/*
*	Funcion: generate code for while statement
*	Parameters: node
*	Return: none
*/
void code_gen_while_stmt(AstNode *node) {
	char *loopBegin = gen_new_label();
	char *loopEnd = gen_new_label();
	emit_label(loopBegin);
	code_gen_expr(node->children[0]);
	char str[512];
	snprintf(str, 512, "beqz $v0, %s", loopEnd);
	emit(str);
	code_gen_stmt(node->children[1]);
	snprintf(str, 512, "b %s", loopBegin);
	emit(str);
	emit_label(loopEnd);
	free(loopBegin);
	free(loopEnd);
}

void code_gen_stmt(AstNode *node){
	if(node == NULL)
		return;
	switch(node->sKind) {
		case COMPOUND_STMT:
			code_gen_stmt(node->children[0]);
			code_gen_stmt(node->sibling);
			break;
		case RETURN_STMT:
			code_gen_expr(node->children[0]);
			emit("jr $ra");
			code_gen_stmt(node->sibling);
			break;
		case EXPRESSION_STMT:
			code_gen_expr(node->children[0]);
			code_gen_stmt(node->sibling);
			break;
		case IF_THEN_ELSE_STMT: {
			if(node->children[2] == NULL)
				code_gen_if_stmt(node);
			else
				code_gen_if_else_stmt(node);
			code_gen_stmt(node->sibling);
			break;
		}
		case WHILE_STMT: {
			code_gen_while_stmt(node);
			code_gen_stmt(node->sibling);
			break;
		}
		default:break;
	}
}

void code_gen_method(AstNode *node){
	if(node->children[1]->nSymbolTabPtr == NULL)
		printf("NULL!\n");
	currentMethod = node;
	code_gen_var();
	code_gen_stmt(node->children[1]);
}

/*
*	Function: generate code for all methods
*	Parameters: none
*	Return: none
*/
void code_gen_all_methods() {
	AstNode *start = program;
	while(start != NULL) {
		while(start != NULL && start->irGenerated == 1)
			start = start->sibling;
		if(start == NULL) {
			return;
		}
		if(strcmp(start->nSymbolPtr->id, "input") == 0 || strcmp(start->nSymbolPtr->id, "output") == 0) {}
		else {
			emit_label(start->nSymbolPtr->id);
			emit("subu $sp, $sp, 8");
			emit("sw $fp, 4($sp)");
			emit("sw $ra, 0($sp)");
			emit("addu $fp, $sp, 4");
			start->irGenerated = 1;
			currentMethod = start;
			code_gen_method(start);
		}
		start = start->sibling;
	}
}

void codegen(){
	emit(".text");
	emit(".align 2");
	emit("\n");
	emit_label("main");
	emit("subu $sp, $sp, 8");
	emit("sw $fp, 4($sp)");
	emit("sw $ra, 0($sp)");
	emit("addu $fp, $sp, 4");
 /* from now on call code gen on the main stmt in main */
	labelCounter = 0;
	ElementPtr mainMethod = symLookup("main");
	if(mainMethod == NULL) {
		printf("Main method not found!\n");
		return;
	}
	mainMethod->snode->irGenerated = 1;
	code_gen_method(mainMethod->snode);
	code_gen_all_methods();
	
	emit("\n\t.data");
}
